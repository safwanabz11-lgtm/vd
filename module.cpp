// Badhon Game Profile - Zygisk module
// Per-app device profile (Build.*, SoC, GPU strings) for game compatibility testing.
// WebView UA / Client Hints follow android.os.Build, so they match the profile automatically.
#include <jni.h>
#include <android/log.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "zygisk.hpp"

#define TAG "BadhonGP"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define MODDIR "/data/adb/modules/badhon_game_profile"

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;
using Map = std::map<std::string, std::string>;
struct Profile { std::string name; Map kv; };

// ---------------- small helpers ----------------
static bool rd(int fd, void *b, size_t n) {
    auto p = (char *)b;
    while (n) { ssize_t r = read(fd, p, n); if (r <= 0) return false; p += r; n -= r; }
    return true;
}
static bool wr(int fd, const void *b, size_t n) {
    auto p = (const char *)b;
    while (n) { ssize_t r = write(fd, p, n); if (r <= 0) return false; p += r; n -= r; }
    return true;
}
static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static Map parseKV(const std::string &t) {
    Map m; std::istringstream is(t); std::string l;
    while (std::getline(is, l)) {
        l = trim(l);
        if (l.empty() || l[0] == '#') continue;
        auto p = l.find('=');
        if (p == std::string::npos) continue;
        m[trim(l.substr(0, p))] = trim(l.substr(p + 1));
    }
    return m;
}
// apps.txt: "package" or "package=ProfileName"
static Map parseApps(const std::string &t) {
    Map m; std::istringstream is(t); std::string l;
    while (std::getline(is, l)) {
        l = trim(l);
        if (l.empty() || l[0] == '#') continue;
        auto p = l.find('=');
        if (p == std::string::npos) m[l] = "";
        else m[trim(l.substr(0, p))] = trim(l.substr(p + 1));
    }
    return m;
}
static std::vector<Profile> parseProfiles(const std::string &t) {
    std::vector<Profile> v; std::istringstream is(t); std::string l;
    while (std::getline(is, l)) {
        l = trim(l);
        if (l.empty() || l[0] == '#') continue;
        if (l[0] == '[' && l.back() == ']') { v.push_back({l.substr(1, l.size() - 2), {}}); continue; }
        auto p = l.find('=');
        if (p == std::string::npos || v.empty()) continue;
        v.back().kv[trim(l.substr(0, p))] = trim(l.substr(p + 1));
    }
    return v;
}
static std::string subst(std::string s, const Map &vars) {
    for (auto &kv : vars) {
        std::string key = "{" + kv.first + "}"; size_t p;
        while ((p = s.find(key)) != std::string::npos) s.replace(p, key.size(), kv.second);
    }
    return s;
}
static uint64_t fnv1a(const std::string &s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
static bool recvStr(int fd, std::string &out) {
    uint32_t n = 0;
    if (!rd(fd, &n, 4) || n > (1u << 22)) return false;
    out.resize(n);
    return n == 0 || rd(fd, &out[0], n);
}

// ---------------- JNI helpers ----------------
static std::string getStr(JNIEnv *env, jclass c, const char *field) {
    jfieldID f = env->GetStaticFieldID(c, field, "Ljava/lang/String;");
    if (!f) { env->ExceptionClear(); return ""; }
    auto s = (jstring)env->GetStaticObjectField(c, f);
    if (!s) return "";
    const char *u = env->GetStringUTFChars(s, nullptr);
    std::string r = u ? u : "";
    env->ReleaseStringUTFChars(s, u);
    env->DeleteLocalRef(s);
    return r;
}
static void setStr(JNIEnv *env, jclass c, const char *field, const std::string &v) {
    if (v.empty()) return;
    jfieldID f = env->GetStaticFieldID(c, field, "Ljava/lang/String;");
    if (!f) { env->ExceptionClear(); return; }   // e.g. SOC_MODEL on Android < 12
    jstring s = env->NewStringUTF(v.c_str());
    env->SetStaticObjectField(c, f, s);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(s);
}

// ---------------- native hooks (best effort) ----------------
static std::map<std::string, std::string> g_props;
static std::string g_gpu_vendor, g_gpu_renderer;
static int (*orig_prop_get)(const char *, char *) = nullptr;
static const unsigned char *(*orig_glGetString)(unsigned int) = nullptr;

static int my_prop_get(const char *name, char *value) {
    if (name && value) {
        auto it = g_props.find(name);
        if (it != g_props.end()) {
            strncpy(value, it->second.c_str(), 91); value[91] = 0;
            return (int)strlen(value);
        }
    }
    return orig_prop_get ? orig_prop_get(name, value) : 0;
}
static const unsigned char *my_glGetString(unsigned int n) {
    if (n == 0x1F00 && !g_gpu_vendor.empty())   return (const unsigned char *)g_gpu_vendor.c_str();
    if (n == 0x1F01 && !g_gpu_renderer.empty()) return (const unsigned char *)g_gpu_renderer.c_str();
    return orig_glGetString ? orig_glGetString(n) : nullptr;
}
static void hookLoadedLibs(Api *api) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return;
    char line[1024]; std::set<std::pair<dev_t, ino_t>> seen;
    while (fgets(line, sizeof line, f)) {
        unsigned long long s, e, off; char perms[8]; unsigned maj, min; unsigned long ino;
        if (sscanf(line, "%llx-%llx %7s %llx %x:%x %lu", &s, &e, perms, &off, &maj, &min, &ino) != 7 || !ino) continue;
        dev_t dev = makedev(maj, min);
        if (!seen.insert({dev, (ino_t)ino}).second) continue;
        api->pltHookRegister(dev, (ino_t)ino, "__system_property_get", (void *)my_prop_get, (void **)&orig_prop_get);
        if (!g_gpu_renderer.empty())
            api->pltHookRegister(dev, (ino_t)ino, "glGetString", (void *)my_glGetString, (void **)&orig_glGetString);
    }
    fclose(f);
    api->pltHookCommit();
}

// ---------------- module ----------------
class Module : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override { api_ = api; env_ = env; }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        active_ = false;
        if (args->uid % 100000 < 10000 || !args->nice_name) { off(); return; }   // skip system / root uids

        const char *nn = env_->GetStringUTFChars(args->nice_name, nullptr);
        std::string pkg = nn ? nn : "";
        env_->ReleaseStringUTFChars(args->nice_name, nn);
        auto c = pkg.find(':'); if (c != std::string::npos) pkg.resize(c);

        int fd = api_->connectCompanion();
        std::string cfgT, appsT, profT;
        bool ok = fd >= 0 && recvStr(fd, cfgT) && recvStr(fd, appsT) && recvStr(fd, profT);
        if (fd >= 0) close(fd);
        if (!ok) { off(); return; }

        Map apps = parseApps(appsT);
        auto ai = apps.find(pkg);
        if (ai == apps.end()) { off(); return; }          // not a target app -> untouched

        Map cfg = parseKV(cfgT);
        std::vector<Profile> profs = parseProfiles(profT);
        if (profs.empty()) { off(); return; }

        std::string want = ai->second;                     // per-app override
        if (want.empty() && cfg["mode"] != "random") want = cfg["profile"];
        size_t idx = 0;
        if (!want.empty()) {
            for (size_t i = 0; i < profs.size(); i++) if (profs[i].name == want) idx = i;
        } else {                                           // stable random: seed + package
            idx = fnv1a(cfg["seed"] + ":" + pkg) % profs.size();
        }
        prof_ = profs[idx]; pkg_ = pkg; log_ = cfg["log"] != "0"; active_ = true;
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (!active_) return;
        JNIEnv *env = env_;
        jclass b = env->FindClass("android/os/Build");
        jclass v = env->FindClass("android/os/Build$VERSION");
        if (!b || !v) { env->ExceptionClear(); return; }

        Map vars{{"release", getStr(env, v, "RELEASE")}, {"real", getStr(env, b, "ID")}};
        std::string id = subst(prof_.kv["build_id"].empty() ? "{real}" : prof_.kv["build_id"], vars);
        vars["id"] = id;
        auto P = [&](const char *k) { return subst(prof_.kv[k], vars); };

        static const char *map[][2] = {
            {"brand", "BRAND"}, {"manufacturer", "MANUFACTURER"}, {"model", "MODEL"},
            {"device", "DEVICE"}, {"product", "PRODUCT"}, {"board", "BOARD"},
            {"hardware", "HARDWARE"}, {"display", "DISPLAY"}, {"fingerprint", "FINGERPRINT"},
            {"soc_manufacturer", "SOC_MANUFACTURER"}, {"soc_model", "SOC_MODEL"}};
        for (auto &m : map) setStr(env, b, m[1], P(m[0]));
        setStr(env, b, "ID", id);

        for (const char *part : {"", "system.", "vendor.", "odm.", "product.", "system_ext."}) {
            std::string p = std::string("ro.product.") + part;
            g_props[p + "brand"] = P("brand");   g_props[p + "manufacturer"] = P("manufacturer");
            g_props[p + "model"] = P("model");   g_props[p + "device"] = P("device");
            g_props[p + "name"] = P("product");  g_props[p + "marketname"] = P("marketname");
        }
        g_props["ro.product.board"] = P("board");
        g_props["ro.build.fingerprint"] = P("fingerprint");
        g_props["ro.build.id"] = id;
        g_props["ro.build.display.id"] = P("display");
        g_props["ro.soc.model"] = P("soc_model");
        g_props["ro.soc.manufacturer"] = P("soc_manufacturer");
        for (auto it = g_props.begin(); it != g_props.end();)
            it = it->second.empty() ? g_props.erase(it) : std::next(it);

        g_gpu_vendor = P("gpu_vendor"); g_gpu_renderer = P("gpu_renderer");
        hookLoadedLibs(api_);
        if (log_) LOGI("%s -> profile '%s' (%s)", pkg_.c_str(), prof_.name.c_str(), P("model").c_str());
    }

    void preServerSpecialize(ServerSpecializeArgs *) override { off(); }

private:
    void off() { api_->setOption(zygisk::DLCLOSE_MODULE_LIBRARY); }
    Api *api_ = nullptr; JNIEnv *env_ = nullptr;
    bool active_ = false, log_ = true; Profile prof_; std::string pkg_;
};

// Root companion: reads config files (apps can't read /data/adb) and sends them over the socket
static std::string slurp(const char *name) {
    std::ifstream f(std::string(MODDIR "/") + name);
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
static void companion_handler(int fd) {
    for (const char *n : {"config.txt", "apps.txt", "profiles.conf"}) {
        std::string d = slurp(n); uint32_t len = (uint32_t)d.size();
        if (!wr(fd, &len, 4) || !wr(fd, d.data(), len)) return;
    }
}

REGISTER_ZYGISK_MODULE(Module)
REGISTER_ZYGISK_COMPANION(companion_handler)
