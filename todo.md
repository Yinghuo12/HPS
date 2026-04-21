重构计划

1. iomanager部分epoll 用unordered_map存储fd和channel，channel中存储fd，这样fd和channel就一一对应了
2. 左值右值问题能否使用完美转发 例如fiberandthread()这里使用了swap
3. 头文件引用 删除sylar.h文件, 把头文件都正确引用到单个.h/.cc文件中
4. socket模块转换字节序更简洁，不使用自定义的endian.h
6. 用map重写http部分
7. pthread封装成类的部分，参考muduo的实现