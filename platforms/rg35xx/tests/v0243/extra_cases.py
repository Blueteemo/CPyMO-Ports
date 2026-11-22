from run243 import run,b
import shutil
p=b/'fixtures/choice';shutil.copytree(b/'fixtures/540x360',p,dirs_exist_ok=True)
(p/'script/start.txt').write_text('#bg test\n#sel 2\n选择第一项\n选择第二项\n#say ,选择结束后恢复剧情。\n#wait 999999\n')
# Bindings already approved by the user: START menu, SELECT settings.
assert run('start_select',events=[(130,8),(180,1),(230,4),(290,1),(350,16),(410,1)],frames=480)==0
assert run('choice_real','choice',events=[(140,32),(180,256),(240,8192),(290,4096)],frames=360)==0
