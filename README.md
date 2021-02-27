# raft
raft based on asio / c++14

#### [2021-2-25]
* 修复同一个term内可能选出多于1个leader的严重bug
* 修复election timeout周期时间过短的bug
* 完成每个状态下处理各种信息的逻辑
* 添加了关于客户端提交command相关的消息类型

##### TODO:
* 完成客户端数据提交的逻辑

#### [2021-2-26]
* 为了便于实现和逻辑的清晰，将心跳包区别于append entries，增加字段hear_beat
TODO:
* 在完成了选主和日志复制的基本逻辑之后，需要完成以下部分的逻辑：
1. 节点的状态持久化，目前只是将日志存储在内存中
2. 节点的加入，退出机制，加入包括新的节点加入和crash节点重启
3. 快照机制（优先级靠后，作为算法优化机制可以最后再实现）
4. pre-vote
5. 保证读的强一致性，需要注意的两个机制：leader在每次被选举出来需要同步一个本term的command，
并将其之前的所有日志应用到状态机后，才能提供读服务；leader需要实现租赁机制，确保分区产生时不存在
两个leader同时服务的可能。
6. 保证写的幂等性，注意raft不保证每个command只被执行一次（实际上只要涉及WAL和不可靠网络通信，就无法存在exactly-once保证）

参考url：
https://www.zhihu.com/question/302761390
https://segmentfault.com/a/1190000038171007?utm_source=tag-newest
https://segmentfault.com/a/1190000038170990?utm_source=tag-newest
https://zhuanlan.zhihu.com/p/130245819
https://zhuanlan.zhihu.com/p/113149149
https://zhuanlan.zhihu.com/p/64405742
https://zhuanlan.zhihu.com/p/22820761
