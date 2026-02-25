#include <cstring>
#include <string>
#include <algorithm>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cinttypes>
#include "hack.h"
#include "zygisk.hpp"
#include "log.h"

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

class MyModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        auto package_name = env->GetStringUTFChars(args->nice_name, nullptr);
        auto app_data_dir = env->GetStringUTFChars(args->app_data_dir, nullptr);
        preSpecialize(package_name, app_data_dir);
        env->ReleaseStringUTFChars(args->nice_name, package_name);
        env->ReleaseStringUTFChars(args->app_data_dir, app_data_dir);
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (enable_hack) {
            std::thread hack_thread(hack_prepare, game_data_dir, data, length);
            hack_thread.detach();
        }
    }

private:
    Api *api;
    JNIEnv *env;
    bool enable_hack = false;
    char *game_data_dir;
    void *data;
    size_t length;

    // 终极读取函数：从 Magisk 模块自身目录中读取包名
    std::string ReadConfigFromModuleDir() {
        int dirfd = api->getModuleDir();  // 获取 Magisk 赋予的高权限模块目录描述符
        if (dirfd < 0) return "";

        // 在模块目录下寻找 gamepackage.txt
        int fd = openat(dirfd, "gamepackage.txt", O_RDONLY);
        if (fd < 0) {
            return "";
        }

        char buffer[128] = {0};
        int bytes_read = read(fd, buffer, sizeof(buffer) - 1);
        close(fd);

        if (bytes_read > 0) {
            std::string pkg(buffer);
            // 强力过滤：清理可能混入的所有空格、回车、换行符
            pkg.erase(std::remove_if(pkg.begin(), pkg.end(), [](unsigned char c) { return std::isspace(c); }), pkg.end());
            return pkg;
        }
        return "";
    }

    void preSpecialize(const char *package_name, const char *app_data_dir) {
        // 调用我们的新函数
        std::string targetPkg = ReadConfigFromModuleDir();

        // 匹配成功！
        if (!targetPkg.empty() && targetPkg == package_name) {
            LOGI("detect game (from config): %s", package_name);
            enable_hack = true;
            game_data_dir = new char[strlen(app_data_dir) + 1];
            strcpy(game_data_dir, app_data_dir);

#if defined(__i386__)
            auto path = "zygisk/armeabi-v7a.so";
#endif
#if defined(__x86_64__)
            auto path = "zygisk/arm64-v8a.so";
#endif
#if defined(__i386__) || defined(__x86_64__)
            int dirfd = api->getModuleDir();
            int fd = openat(dirfd, path, O_RDONLY);
            if (fd != -1) {
                struct stat sb{};
                fstat(fd, &sb);
                length = sb.st_size;
                data = mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0);
                close(fd);
            } else {
                LOGW("Unable to open arm file");
            }
#endif
        } else {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }
};

REGISTER_ZYGISK_MODULE(MyModule)
