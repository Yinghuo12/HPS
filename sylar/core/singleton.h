#ifndef __SYLAR_SINGLETON_H__
#define __SYLAR_SINGLETON_H__

#include <memory>

namespace sylar {

// 通过 X 和 N 参数，可给同一类型创建多个独立单例：例如下面两行代码分别创建了两个 LoggerManager单例，GetInstance()返回的就是不同的实例
// using LoggerMgr1 = Singleton<LoggerManager, void, 1>;
// using LoggerMgr2 = Singleton<LoggerManager, void, 2>;

template<class T, class X = void, int N = 0>
class Singleton {
public:
    static T* GetInstance() {
        static T v;
        return &v;
    }
};

template<class T, class X = void, int N = 0>
class SingletonPtr {
public:
    static std::shared_ptr<T> GetInstance() {
        static std::shared_ptr<T> v(new T);
        return v;
    }
};

}

#endif