v0.24.5 视觉微调：正文额外内缩由0.5em回到0.4375em，约左回2像素；时钟额外右收画布高1/60（约7–8输出像素）；列表指南16→18。继承inset5几何/分页/人名检查，新增三画布×三字号时钟实际墨迹左界、指南实际高度增大及边界检查。
Linux重现：python3 tests/v0245_polish6/run_render.py ASAN_OBJECT_DIR FONT FIXTURE。preview.c用真实SYSTEM皮肤和640×480渲染器生成三档图。软件验证不代表真机验收。
