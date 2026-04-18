重构计划

1. iomanager部分epoll 用unordered_map存储fd和channel，channel中存储fd，这样fd和channel就一一对应了
2. 左值右值问题能否使用完美转发 例如fiberandthread()这里使用了swap
3. 头文件引用 删除sylar.h文件, 把头文件都正确引用到单个.h/.cc文件中
4. socket模块转换字节序更简洁，不使用自定义的endian.h
5. 日志系统异步优化 [【sylar-webserver】重构日志系统](https://blog.csdn.net/weixin_52355727/article/details/147396933?utm_medium=distribute.pc_relevant.none-task-blog-2~default~baidujs_baidulandingword~default-1-147396933-blog-123035455.235^v43^pc_blog_bottom_relevance_base7&spm=1001.2101.3001.4242.2&utm_relevant_index=3)
6. 用map重写http部分
7. pthread封装成类的部分，参考muduo的实现