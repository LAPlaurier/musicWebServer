import glob
import os

all_files = glob.glob('new_root/music/*.mp3')
print(all_files)

chinese_name = ['花海', '蒲公英的约定', '不能说的秘密','孤勇者','命运','错位时空','消愁','山楂树之恋',
                '天外来物','多远都要在一起','可惜没如果','沈园外']
path = 'new_root/music'

for i, file in enumerate(all_files):
    # 获取旧文件名（就是路径+文件名）
    old_name = file  # os.sep添加系统分隔符

    if os.path.isdir(old_name):  # 如果是目录则跳过
        continue
 
    # 设置新文件名
    index = int(file[-6:-4])
    new_name = path + os.sep + chinese_name[index-1] + '.mp3'
    os.rename(old_name, new_name)  # 用os模块中的rename方法对文件改名

all_files = glob.glob('new_root/cover/*.jpg')

chinese_name = ['花海', '蒲公英的约定', '不能说的秘密','孤勇者','命运','错位时空','消愁','山楂树之恋',
                '天外来物','多远都要在一起','可惜没如果','沈园外']
path = 'new_root/cover'

for i, file in enumerate(all_files):
    # 获取旧文件名（就是路径+文件名）
    old_name = file  # os.sep添加系统分隔符
    if os.path.isdir(old_name):  # 如果是目录则跳过
        continue
 
    # 设置新文件名
    index = int(file[-6:-4])
    new_name = path + os.sep + chinese_name[index-1] + '.jpg'
    os.rename(old_name, new_name)  # 用os模块中的rename方法对文件改名

