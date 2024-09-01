# %%
import re
import csv
# import glob
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import math

#%%

def parser_log(filename):

    # 打开日志文件并读取内容
    with open(filename, 'r') as file:
        log = file.read()

    # 使用正则表达式分割日志文件
    sections = re.split(r'-{48}', log)

    data_list = []

    for section in sections:
        if section == '':
            continue
        # 使用正则表达式匹配需要的信息
        workload = re.search(r'workload: (\w+),', section)
        threads = re.search(r'threads: (\d+)', section)
        loaded_throughput = re.search(r'Throughput: load, ([\d\.]+) Kops/s', section)
        run_throughput = re.search(r'Throughput: run, ([\d\.]+) Kops/s', section)
        dram_consumption = re.search(r'DRAM consumption: ([\d\.]+) MB.', section)
        page_size = re.search(r'PAGE= (\d+) Bytes', section)
        write_read_count = re.search(r'Write_count=\s+(\d+) read_count=\s+(\d+)', section)
        filesize = re.search(r'fielsize=\s+(\d+)', section)
        zone_read_written = re.search(r'\[Zone\] Read:\s+(\d+) Units, Written:\s+(\d+) Units', section)
        load_size = re.search(r'Load size: (\d+),', section)
        run_size = re.search(r'Run size: (\d+)', section)

        # 如果所有信息都找到了，就创建一个字典来存储这些信息
        # if workload and threads and loaded_throughput and run_throughput and dram_consumption and page_size and write_read_count:
        try:
            data = {
                'workload': workload.group(1),
                'threads': threads.group(1),
                'loaded_keys': load_size.group(1),
                'running_keys': run_size.group(1),
                'loaded_throughput': loaded_throughput.group(1),
                'run_throughput': run_throughput.group(1),
                'dram_consumption': dram_consumption.group(1),
                'page_size': page_size.group(1),
                'write_count': write_read_count.group(1) ,
                'read_count':write_read_count.group(2),
                'filesize': filesize.group(1),
                'zone_read': float(zone_read_written.group(1))*1000*512/(int(page_size.group(1))),
                'zone_written': float(zone_read_written.group(2))*1000*512/(int(page_size.group(1)))
            }
            data_list.append(data)
        except Exception as e:
            print(e)
            continue
            
    
    output_csv_file=filename.replace('.log','.csv')
    # 将字典列表写入CSV文件
    with open(output_csv_file, 'w', newline='') as file:
        writer = csv.DictWriter(file, fieldnames=data_list[0].keys())
        writer.writeheader()
        for data in data_list:
            writer.writerow(data)
        print(f"parser data {output_csv_file} success!")

# %%
# file_list = glob.glob('./result/log/*.log')
# for file in file_list:
    # parser_log(file)

# 读取CSV文件
# print(file_list[0].replace('.log','.csv'))
# df = pd.read_csv(file_list[0].replace('.log','.csv'))

def plot_ycsb(file):
    df = pd.read_csv(file)
    print(file)
    plt.figure()
    # 定义柱子的宽度
    bar_width = 0.2

    # 生成柱子的位置
    r1 = np.arange(len(df['workload']))
    r2 = [x + bar_width for x in r1]
    r3 = [x + bar_width for x in r2]
    r4=[x + bar_width for x in r3]
    r5=[x + bar_width for x in r4]

    bars1 = plt.bar(r2, df['write_count'], width=bar_width, label='write_count')
    bars2 = plt.bar(r3, df['read_count'], width=bar_width, label='read_count')
    bars3 = plt.bar(r1, df['filesize'], width=bar_width, label='filesize')
    # bars4 = plt.bar(r3, df['zone_read'], width=bar_width, label='zone_read')
    # bars5 = plt.bar(r4, df['zone_written'], width=bar_width, label='zone_written')
    # 在柱子上添加标签
    i=0
    for bar in bars2:
        yval = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2, yval, round(yval/df['filesize'][i], 0), ha='center', va='bottom')
        i+=1
    i=0
    for bar in bars1:
        yval = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2, yval, round(yval/df['filesize'][i], 0), ha='center', va='bottom')
        i+=1
    i=0
    # for bar in bars4:
    #     yval = bar.get_height()
    #     plt.text(bar.get_x() + bar.get_width()/2, yval, round(yval/(df['filesize'][i]), 0), ha='center', va='bottom')
    #     i+=1
    # i=0
    # for bar in bars5:
    #     yval = bar.get_height()
    #     plt.text(bar.get_x() + bar.get_width()/2, yval, round(yval/(df['filesize'][i]), 0), ha='center', va='bottom')
    #     i+=1



    # 设置x轴的标签
    plt.xticks([r + bar_width for r in range(len(df['workload']))], df['workload'])

    # 设置图像的标题和坐标轴标签
    plt.title(f"PageSize: {file.split('-')[-2]} Load 1M Run 1M Pairs")
    plt.xlabel('Workload')
    plt.ylabel('Relative Value')

    # 添加图例
    plt.legend()

    # 显示图像
    plt.show()
# %%
# for file in file_list:
    # plot_ycsb(file.replace('.log','.csv'))
# %%

def ceil_to_n(x, N):
    return int(math.ceil(x / N)+1) * N
def plot_wa():

    fig=plt.figure(figsize=(4.20, 2.40),dpi=900)
    ax1= fig.add_subplot(111)

    xlabels=["B$^{+}$Tree","B$^{+}$Tree+FS","B$^{+}$Tree+FS+Cov-SSD"]
    y1=[2081.86,3905.73,5233.67]
    # y2=[2038.42,4077.06,4077.06]
    colors=['#d9c8ad','#546170']
    # 生成柱子的位置
    bar_width = 0.3
    r1 = [0.25,0.75,1.25]
    print(r1)
    bars=ax1.bar(r1, y1, width=bar_width ,label='Conv-SSD',hatch='xx',facecolor='none',edgecolor='steelblue')

    # 设置x轴的标签
    # plt.xticks([r + bar_width*3/2 for r in r1], xlabels)
    plt.xticks([r for r in r1], xlabels,rotation=1)
    plt.grid(axis='y',linestyle='--',linewidth=0.5)
    
    # 为每个柱子增加数值标签
    for bar in bars:
        height = bar.get_height()
        ax1.text(bar.get_x() + bar.get_width() / 2, height,
                 '%4.2f' % (height), ha='center', va='bottom')

    # 设置图像的标题和坐标轴标签
    # plt.title(r"Breakdown of B$^{+}$Tree on SSD-based Systems")
    plt.ylabel('Total Writes (MiB)')
    plt.ylim(000,4000)
    # plt.yticks([0,1000,2000,3000])

    # 添加图例
    # plt.legend(loc="upper left",bbox_to_anchor=(-0.01,1.02))
    fname="./test/result/pic/"
    plt.savefig(fname+"figures-0-wa.pdf",bbox_inches='tight',dpi=900)
    # 显示图像
    # plt.show()

# %%
def plot_wa2():

    fig=plt.figure(figsize=(4.20, 2.40),dpi=900)
    ax1= fig.add_subplot(111)

    xlabels=["Index layer","FS & Flash layer"]
    y1=[151.07,233.98]
    y2=[11.36,13.50]
    colors=["#ce383e",'#135D66','#546170']
    hatchs=['xx','++','///','-\\']
    # 生成柱子的位置
    bar_width = 0.12
    r1 = [0.3,0.6]
    r2=[x+bar_width*1 for x in r1]    
    def add_ytext(barrrr):
        for bar in barrrr:
            height = bar.get_height()
            ax1.text(bar.get_x() + bar.get_width() / 2+0.01, height,
                '%4.2fX' % (height), ha='center', va='bottom',fontdict={'fontsize':14})
    
    bars=ax1.bar(r1, y1, width=bar_width ,label=r'B$^{+}$Tree',hatch=hatchs[0],facecolor='none',edgecolor=colors[0])
    add_ytext(bars)
    bars=ax1.bar(r2, y2, width=bar_width ,label='LSM-Tree',hatch=hatchs[1],facecolor='none',edgecolor=colors[1])
    add_ytext(bars)
    # 设置x轴的标签
    plt.xticks([r+bar_width*0.5 for r in r1], xlabels,rotation=1,fontsize=16)
    plt.grid(axis='y',linestyle='--',linewidth=0.5)
    plt.yticks([0,100,200,300],fontsize=14)
    plt.ylabel('Cumulative WA',fontsize=16)
    # 添加图例
    plt.legend(loc="upper left",bbox_to_anchor=(-0.02,1.04),fontsize=15)
    fname="./test/result/pic/"
    plt.savefig(fname+"figures-0-wa2.pdf",bbox_inches='tight',dpi=900)

# %%

def plot_f2fs():
    """
    Motivation: the low utilization of f2fs on ZNS SSD for B+Tree
    """
    plt.figure(figsize=(4.20, 2.40),dpi=900)
    xlabels=["4KiB","8KiB","16KiB","32KiB","64KiB"]
    bar_width = 0.35
    r1 = np.arange(len(xlabels))
    r2 = [x + bar_width for x in r1]
    r3 = [x + bar_width for x in r2]
    r4=[x + bar_width for x in r3]
    y1=[476,688,948,1002,987]
    y2=[775,1425,1966,1920,1907]
    result = [a / b for a, b in zip(y1, y2)]
    print(result)
    colors=['goldenrod','steelblue']


    bars1 = plt.bar(r2, y1,hatch="///",facecolor='none',edgecolor=colors[0], width=bar_width,label=r'F2FS-ZNS')
    bars2 = plt.bar(r3, y2,hatch="xx" ,facecolor='none',edgecolor=colors[1],width=bar_width, label='Raw-ZNS-SSD')

    # 设置x轴的标签
    plt.xticks([r + bar_width*3/2 for r in r1], xlabels)
    plt.grid(axis='y',linestyle='--',linewidth=0.5)

    # 设置图像的标题和坐标轴标签
    # plt.title(r"Breakdown of B$^{+}$Tree on SSD-based Systems")
    plt.ylabel('Throughput(MiB/s)')
    plt.xlabel('Write granularity')

    # 添加图例
    plt.legend(loc="upper left",bbox_to_anchor=(-0.01,1.02))
    fname="./test/result/pic/"
    plt.savefig(fname+"figures-0-f2fs-util.png",bbox_inches='tight',dpi=900)


# %%    

def plot_buffer():
    """
    Motivation: the low effects of BufferPool on ZNS SSD for B+Tree
    """
    fig=plt.figure(figsize=(4.20, 2.40),dpi=900)
    ax1= fig.add_subplot(111)
    xlabels=["1M","2M","4M","8M","16M"]
    x = np.arange(len(xlabels))
    bar_width = 0.55
    y1 = [440.32,2047.74,3476.74,4451.97,4995.87]
    y2 = [678.622585,185.337689,123.772938,105.650867,100.632778]
    # colors=['#cdd8ea','#fce49e']
    colors=['#d7c8af','#546170']
    # colors=['steelblue','goldenrod']
    # 创建柱状图
    # bars = plt.bar(x, y1, color=colors[0],facecolor=colors[0],label="Write Amplification")
    bars=ax1.bar(x, y1,width=bar_width, hatch="xx",facecolor='none',edgecolor='steelblue',label="Write Bytes")
    # ax1.bar(x, y1,width=bar_width, facecolor='none',edgecolor='#546170')
    plt.xticks(x, xlabels)
    plt.xlabel("The number of inserted key-value pairs")
    plt.ylabel("Write Bytes Per Insertion")
    # 创建第二个y轴
    ax2=plt.twinx()
    # 创建折线图
    lines = plt.plot(x, y2, marker='H', ls='dashed',color=colors[1],label='Write IOPS',markersize=10)
    # 设置第二个y轴的刻度范围
    ax2.set_ylim(0, ceil_to_n(max(y2),200))
    ax2.set_ylabel("KIOPS/s")

    # 添加图例
    plt.legend(handles=[bars, lines[0]],loc="upper left",bbox_to_anchor=(-0.01,1.02))
    # plt.legend(loc="upper left",bbox_to_anchor=(-0.01,1.02))
    fname="./test/result/pic/"
    plt.savefig(fname+"figures-1-bufferpool-util.png",bbox_inches='tight',dpi=900)


# %%
def get_diff(data):
    # 初始化对比对象
    compare_obj = [0,0]

    # 初始化差值列表
    diffs = []

    # 遍历data
    for index, row in data.iterrows():
        # 如果第一列的元素重复则跳过
        if compare_obj[0] == row[0]:
            continue
        # 当出现不重复的第一列元素，计算其差值
        elif compare_obj is not None:
            diff = (row[0] - compare_obj[0])/(row[1]-compare_obj[1])
            diffs.append(diff)
        # 将第一个不重复元素作为新的对比对象
        compare_obj = row
    return diffs[2:]


def plot_conv():
    """
    Motivation: the drawbacks of cov-SSD
    """
    fig=plt.figure(figsize=(4.20, 2.40),dpi=900)
    ax1= fig.add_subplot(111)
    colors=['#e9791f','#9ebcda']
    colors.reverse()
    # 绘制折线图
    # data = pd.read_csv('./test/result/log/4-trim-rand-P4510-nvme2n1-iostat-4k.csv')
    data = pd.read_csv('./test/result/log/6-trim-rand-conv-write-P4510-nvme2n1-iostat-4k.csv')
    # data_smooth = data.rolling(window=1).mean()
    data_smooth = data
    step=1
    ax1.plot(data_smooth[:-1:step],marker='|',linestyle='-.',color=colors[1],label='BW-Con-SSD')
    # random_data = np.random.normal(loc=700, scale=2, size=data_smooth.shape)
    zns_data = pd.read_csv('./test/result/log/4-zns-write-SN540-nvme0n2-iostat-4k.csv')
    # zns_data = pd.concat([zns_data, zns_data], ignore_index=True)
    print(data.mean(),zns_data.mean())
    # ax1.plot(np.arange(0,data.size-2,2),zns_data,marker='|',linestyle='-',color=colors[0],label='Bandi-ZNS')
    zns_data=zns_data
    ax1.plot(zns_data[:-1:step],marker='|',linestyle='-',color=colors[0],label='BW-ZNS-SSD')
    plt.xticks(fontsize=14)
    plt.xlim(-100,6100)
    ax1.set_xlabel('Time (s)',fontsize=14)
    plt.yticks(fontsize=14)
    ax1.set_ylabel('Bandwidth (MiB/s)',fontsize=14)
    ax1.set_ylim(-100, 1400)
    ax1.set_yticks([0,400,800,1200])

    # 创建第二个y轴
    ax2=plt.twinx()

    # data = pd.read_csv('./test/result/log/4-trim-rand-P4510-nvme2n1-smart-4k.csv')
    data = pd.read_csv('./test/result/log/6-trim-rand-conv-write-P4510-nvme2n1-smart-4k.csv')
    # 计算每一行的两列数值与上一行的两列数值的差，并除以上一行的数值
    result = get_diff(data)
    result = pd.DataFrame(result).rolling(window=3).mean()
    x_range=np.linspace(0,data_smooth.size,result.size)
    # print(result)
    # result[14]=1.07
    # result[23]=1.62
    ax2.plot(x_range,result,label='WA-Con-SSD',marker='X',markersize=5,linestyle='-.',color=colors[1],markerfacecolor='#970008')
    
    ax2.plot(x_range,np.ones_like(result),label='WA-ZNS-SSD',marker='X',markersize=5,linestyle='solid',color=colors[0],markerfacecolor='green')

    ax1.grid(axis='y',linestyle='--',linewidth=1)
    ax2.set_ylabel('Write amplification',fontsize=14)
    plt.yticks(fontsize=14)
    ax2.set_ylim(0.8,3.35)
    ax2.set_yticks([1,2,3.0])
    # ax2.set_xlim(0, 3800)
    # ax2.set_xlabel('Time (min)')
    fig.legend(loc="upper left",bbox_to_anchor=(0.075,1.15),ncols=2,fontsize=12)
    fname="./test/result/pic/"
    fig.savefig(fname+"figures-0-conv7.pdf",bbox_inches='tight',dpi=900)

def plot_3a_app_motivate():
    """
    Evaluation: comparsion with app
    """
    plt.figure(figsize=(4.20, 2.40),dpi=900)
    
    bar_width = 0.35
    # xlabels=["load","amzn","osm","wiki","facebook"]
    xlabels=["w10","w8r2","w5r5","w2r8","r10"]
    r1 = np.arange(len(xlabels))
    zbtree=[507.54,214.83,291.59,267.30,400.03] 
    wtiger=[90.69,68.72,107.90,154.58,341.10]
    rocks=[460.08,133.40,142.34,119.78,179.29] 
    xlabels.reverse(),zbtree.reverse(),wtiger.reverse(),rocks.reverse()
    # colors=[,'#333333',,]
    colors=["#ce383e",'#135D66',"#4383bb",'#112D4E','#704264','#393646','goldenrod','#DD5746','#4F4557']
    # colors=['#393646','#666666','#333333',]
    hatchs=['xx','++','///','-\\']

    trees=[wtiger,rocks,zbtree]
    # tree_name=['WiredTiger','RocksDB',r'Z$^{+}$Tree']
    tree_name=['B-Tree','LSM-Tree']
    for i,tree in enumerate(tree_name):
        plt.bar([x+bar_width*i for x in r1], trees[i], hatch=hatchs[i],facecolor='none',edgecolor=colors[i], width=bar_width,label=tree) 

    # 设置x轴的标签
    plt.xticks([r + bar_width*0.5 for r in r1], xlabels,fontsize=18,rotation=10)
    # plt.ylim(0,ceil_to_n(max(zbtree),50))
    plt.yticks([0,200,400],fontsize=16)

    # 设置图像的标题和坐标轴标签
    plt.ylabel('IOPS (Kops/s)',fontsize=18)
    plt.grid(axis='y', linestyle='dashdot')

    # 添加图例
    plt.legend(loc="upper left",bbox_to_anchor=(0.16,1.05),ncol=1,fontsize=18)
    fname="./test/result/pic/"
    plt.savefig(fname+"figures-7-app-motivate.pdf",format='pdf',bbox_inches='tight',dpi=900)

# %%


if __name__ == '__main__':
    # plot_wa()
    plot_wa2()
    # plot_conv()
    # plot_f2fs()
    # plot_buffer()
    plot_3a_app_motivate()
# %%
