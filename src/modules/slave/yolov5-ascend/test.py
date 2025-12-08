import acl, time
from acl import mdl
print("init:", acl.init())
ret = mdl.load_from_file("./ascend/yolov5s.om")
print("load ret:", ret)
model_id = ret[0] if isinstance(ret, tuple) else ret
time.sleep(0.2)
print("unload ret:", mdl.unload(model_id))
print("finalize ret:", acl.finalize())
print("done")

