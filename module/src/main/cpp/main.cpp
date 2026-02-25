#include <cstring>
#include <string>      // 新增：处理字符串
#include <fstream>     // 新增：读取文件
#include <algorithm>   // 新增：处理不可见字符
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cinttypes>
#include "hack.h"
#include "zygisk.hpp"
// #include "game.h"   // 删除了这个头文件，因为我们不再需要写死包名了
#include "log.h"

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

// ==============================================================
// 新增的辅助函数：从 /data/local/tmp/gamepackage.txt 读取目标包名
// ==============================================================
std::string GetTargetPackageName() {
    std::string packageName = "";
    // 以只读模式打开文件
    std::ifstream file("/data/local/tmp/gamepackage.txt");
    
    if (file.is_open()) {
        std::getline(file, packageName);
        
        // 过滤掉文件中可能存在的换行符(\r, \n)和空格，防止影响包名匹对
        packageName.erase(std::remove(packageName.begin(), packageName.end(), '\r'), packageName.end());
        packageName.erase(std::remove(packageName.begin(), packageName.end(), '\n'), packageName.end());
        packageName.erase(std::remove(packageName.begin(), packageName.end(), ' '), packageName.end());
        
        file.close();
    }
    return packageName;
}
// ==============================================================

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
    bool enable_hack;
    char *game_data_dir;
    void *data;
    size_t length;

    void preSpecialize(const char *package_name, const char *app_data_dir) {
        // ==============================================================
        // 修改的核心部分：调用上面的函数获取你写的包名，并和当前启动的 app 对比
        // ==============================================================
        std::string targetPkg = GetTargetPackageName();
        
        // 如果文件读取成功（不为空），并且当前的包名和文件里的一致
        if (!targetPkg.empty() && targetPkg == package_name) {
            LOGI("detect game: %s (Read from /data/local/tmp/gamepackage.txt)", package_name);
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
            // 如果不是目标游戏，则卸载模块不生效
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }
};

REGISTER_ZYGISK_MODULE(MyModule)
