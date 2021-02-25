# raft
raft based on asio / c++14

#### [2021-2-25]
* 修复election timeout周期时间过短的bug
* 完成每个状态下处理各种信息的逻辑
* 添加了关于客户端提交command相关的消息类型

##### TODO:
* 完成客户端数据提交的逻辑
* 完成每个节点相关数据的持久化存储（voted_for, current_term, logs等）
