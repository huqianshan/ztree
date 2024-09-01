# ToDo List

## Buffer Base BTree

### Buffer Pool

- [x] buffer pool的命中次数、命中率
- [ ] buffer pool 的mapping table使用了`STL unordered_map`性能很低,参考perf 图
- [ ] buffer pool的并发竞争问题
- [ ] buffer pool的刷回读取问题：**异步**、顺序预取、提前刷回、且在zns上无法并发刷回

### Zone BTree

- [x] 将中间结点都放置于DRAM之中
- [ ] 修改叶子结点`page_id`为 `zone_id+offset`
  - [ ] 分配叶子结点id需要一个分配算法
- [x] `Optimistic Lock Coupling`

### Buffer Tree

- [ ] `Write-ahead-log`的实现
- [x] `Memtable`的实现: ~~`skiplist`~~ or Btree-OLC total in memory`
  - [ ] 添加 `batch insert/delete/update`接口
  - [ ] `buffer_btree` 分为 `insert`,`evict`,`flush`三个阶段:
    - [ ]  **[内存占用]** 目前`flush`阶段之后，淘汰的结点只是执行`count=0`,仍未从`buffer tree`中删除，导致占用大量内存,平均buffer_tree的叶子结点数量正比于负载。
      - ~~解决办法-1: 删除叶子结点时，删除其在父亲结点的指针，回收此内存空间,难点: 移动中间结点的键值对，需要上写锁，同时将其所有叶子结点锁获取；按照每个inner node记录access_count,淘汰时直接将最冷的inner node的所有子结点刷新，就可以方便加减锁。但是同样会阻塞根节点~~ 
      - 解决办法-2: 将键值对区域通过指针动态分配，删除时将指针的内存区域回收，指针置为空，缺点：浪费`(8+leaf_size)*(leaf_nums)=(24*15w)=3.5MB`内存, 低于10MB的浪费，可以接受。
    - [ ]  **[并发问题]** 目前`evict\flush`两阶段会将所有前台工作线程阻塞，以获取该被淘汰的结点和刷新。将buffer tree理解为缓冲小水池，basetree为大水塘，那么阻塞是合理的，缓存池已经满了就不应该继续让其接受新的请求。关键是增加从小水池流向大水池的效率，也就是批处理不能阻塞且并发。
      - `evict`阶段: 选择N个最少访问的叶子结点;  ~~希望在插入阶段就完成选取，而不是在淘汰阶段阻塞遍历整个树。 ~~
      - `flush`阶段：批处理不能单线程执行，会导致basetree的刷盘过慢，没有充分利用闪存带宽
      - 解决方法1: ~~添加N个槽Slots，用于存放指向该淘汰的叶子结点，初始化为UINT_MAX. 创建一个最大值为N的原子变量next_slot_。插入的操作完成之后，通过fetch_and_add操作更新next_slot_, 与Slots[next_slot_]比较，如果小于则更新。~~ 理论上，实现一个长度为N的无锁的LFU队列符合要求; 启动批处理时，可以预留N个刷新线程，分将其划分给其执行。                                                                                                                                                        
    - [ ]  [淘汰策略] 目前淘汰策略通过优先队列选择`max_leaf_count - keep_leaf_count`个淘汰，这两个参数的选择比较麻烦，可以改为按照内存使用量设置 `MAX_BUFFER_LEAF_MB` 和 `MAX_KEEP_LEAF_MB`

- [ ] 读操作性能比写性能差很多
- [ ] 
### ZNS Storage

- [ ] `Zone`的元数据、以及基于元数据的`Zone`的分配策略
- [ ] 基于元数据的`Zone`回收策略



