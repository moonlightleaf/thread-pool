# thread-pool
C++11以上版本的线程池，v1->v2->v3，简易版迭代到翔实版，使用thread、mutex、condition_variable、future、functional，参照高star实现

---

V1

最简易版本线程池，每个任务都无返回值，线程池容量通过有参构造函数设定为固定值。

---

V2

可以获取每个任务的返回值，通过使用future和functional实现。

---

V3

增加一个管理线程，可根据任务数和工作线程的比值，扩大或缩小线程池的容量。通过构造函数设置线程池的最小和最大线程容量。
