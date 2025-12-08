# lite edge cluster scheduler

## TODO
动态地址初始化 master ip 以参数在编译中传入  
统一模型输入/输出路径  
讨论第三方包依赖 z3求解器等  
检查模型推理内存泄漏问题  


## 配置\&构建

### 配置
for Atlas 310
```sh
cmake -S . -B build -DAGENT_DEVICE_TYPE=ATLAS_I
```

### 构建

编译源代码(4线程)
```sh
cmake --build build -j 4
```
