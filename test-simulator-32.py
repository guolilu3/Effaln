# 2023/10/11修改
# 测试不同长度的read

import time
import os
import sys
import subprocess

len=sys.argv[1]                                                                  #read长度作为参数传入

read="/home/lab/gll/mason_simulator/reads-1M-"+len+".fa"                         #750-bp
out0="/home/lab/gll/mason_simulator/effaln-1M-"+len+"-32.sam"
samacc="/home/lab/gll/protest/mycode/samStatics_gold-1M-"+len

exe0="/home/lab/gll/Effaln-20231011-lens/effaln"
idx0="/home/lab/gll/Effaln-20231011-lens/example-hg19/index-32-r128/hg19"

def nnc(str1):
    p=subprocess.Popen(str1,shell=True)
    print(str1)
    p.communicate()

def test0():
    cmd=exe0+" -x "+idx0+" -f -U "+read+" -S "+out0                    #调用
    start=time.time()
    nnc(cmd)
    end=time.time()
    take0=end-start
    print('running time: ########## %f s' %(take0))
    print(' ')
    nnc("/home/lab/gll/protest/mycode/samStaticsFlag4 "+out0)          #1.比对率
    print(' ')
    nnc(samacc+" "+out0)                                               #2.准确率
    return take0

if __name__=="__main__":
    # print('\n')
    print("----------testing 0  "+time.strftime('%Y-%m-%d %H:%M:%S',time.localtime(time.time())))
    take0=test0()