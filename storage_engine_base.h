#ifndef STORAGE_ENGINE_BASE_H
#define STORAGE_ENGINE_BASE_H

namespace raft {
struct NodeState {

};

class StorageEngineBase {
public:
  //当节点首次启动或者recover时，调用该函数，恢复状态
  virtual void Init() = 0;
};
}

#endif // STORAGE_ENGINE_BASE_H
