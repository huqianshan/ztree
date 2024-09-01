# %%
# import re
# import csv
# import glob
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import math


# %%
def ceil_to_n(x, N):
    return int(math.ceil(x / N)) * N


def plot_micro_base(tree_lists,ycsb_flag=True):
    workload=[]
    # skip base coloums for skewed workloads
    base=0
    store_name=""
    if ycsb_flag:
        workload = [r'YCSB-a(W100)', r'YCSB-b(W80R20)',  r'YCSB-c(W20R80)',r'YCSB-d(R100)', r'YCSB-e(S95W5)']
        store_name="figures-2-ycsb.pdf"
    else:
        base=5
        workload = [r'Skew-a(U100)', r'Skew-b(U50R50)',  r'Skew-c(U5R95)',r'Skew-d(R100)', r'Skew-e(S95U5)']
        store_name="figures-2-skewed.pdf"

    x = [1, 8, 16, 24 ,32, 40, 48,56]  # thread_nums
    tree_name=['conv_btree','f2fs_btree','cow_btree',r'ztree']
    color_map = [ "#4383bb", "#89c983","#c66dbb","#ce383e","#f7bf8a", "#b3b3b3"]
    # color_map.reverse()
    markers=['p','*','>','D','s','>','<']

    fig, axes = plt.subplots(1, len(workload), figsize=(12.3, 2.05), dpi=900)
    # plt.subplots_adjust(left=0.05,bottom=0.05,top=0.95,right=0.95)
    plt.subplots_adjust(wspace=0.29)
    bottom_x=-30
    for i, w in enumerate(workload):
        print(workload[i])
        for j,tree in enumerate(tree_lists):
            axes[i].plot(x, tree.iloc[i+base],color=color_map[j], marker=markers[j],label=tree_name[j], linewidth=2, markersize=9, ls="--")
            print("  ", tree_name[j],": ", tree.iloc[i+base].values.tolist())
        axes[i].grid(axis='y')
        axes[i].set_title(w, size=15)
        axes[i].set_xticks(x)
        axes[i].tick_params(labelsize=10)
        axes[i].set_xlim(min(x)-3,ceil_to_n(max(x), 3)+3)
        axes[i].set_ylim(bottom_x,ceil_to_n(max(tree.iloc[i+base]), 100))
        if workload[i].find("Skew-a")!=-1:
            axes[i].set_ylim(bottom_x,750)
        elif workload[i].find("Skew-e")!=-1:
            axes[i].set_ylim(-10,150)
        
        """ if workload[i].find("YCSB-b")!=-1:
            axes[i].set_ylim(bottom_x,400)
        elif workload[i].find("YCSB-e")!=-1:
            axes[i].set_ylim(bottom_x,300)
        elif workload[i].find("Skew-d")!=-1:
            or workload[i].find("Skew-e")!=-1:
            axes[i].set_ylim((bottom_x,200)) """
    plt.legend(labels=tree_name, loc="upper center",  bbox_to_anchor=(
        -2,-0.15), ncol=len(tree_name), fontsize=12)
        
    axes[0].set_ylabel('Miops', size=14)
    axes[0].set_xlabel('Threads', size=14)
    path="./test/result/pic/"
    fig.savefig(path+store_name, format='pdf', bbox_inches='tight', transparent=False)

def plot_micro():
    """
    ycsb and skewed workload
    """
    dir_name="./test/result/ycsb-3/"
    header_name=['workload','tid-1','tid-8','tid-16','tid-24','tid-32','tid-40','tid-48','tid-56']
    file_lists=["0-conv-btree-ycsb.csv","1-f2fs-btree-ycsb.csv","2-cow-btree-ycsb.csv","3-ztree-ycsb.csv"]
    tree_lists=[]
    for base_name in file_lists:
        file_name=dir_name+base_name
        tree_lists.append(pd.read_csv(file_name,names=header_name,index_col=0))
    # print(tree_lists[3])
    # print(tree_lists[0])

    plot_micro_base(tree_lists,True)
    # skewed
    plot_micro_base(tree_lists,False)

# %%
def plot_wa_sensi():
    """
    Evaluation of Write Amplification
    """
    plt.figure(figsize=(4.20, 2.40),dpi=900)
    
    bar_width = 0.19
    xlabels=["Read","Write"]
    r1 = np.arange(len(xlabels))
    r2 = [x + bar_width for x in r1]
    r3 = [x + bar_width for x in r2]
    r4=[x + bar_width for x in r3]
    r5=[x + bar_width for x in r4]
    y1=[2055.94,3299.46]
    y2=[2057.22,4161.02]
    y3=[2074.37,2260.22] 
    y4=[373.50,415.49] 
    colors=['#135D66','steelblue','goldenrod','#944E63','#4F4F4F']
    hatchs=['///','\\\\','xx','++']

    tree_name=['conv_btree','f2fs_btree','cow_btree','ztree']
    bars1 = plt.bar(r2, y1,hatch=hatchs[0],facecolor='none',edgecolor=colors[0], width=bar_width,label=tree_name[0])
    bars2 = plt.bar(r3, y2,hatch=hatchs[1] ,facecolor='none',edgecolor=colors[1],width=bar_width,label=tree_name[1])
    bars3 = plt.bar(r4, y3,hatch=hatchs[2] ,facecolor='none',edgecolor=colors[2],width=bar_width, label=tree_name[2])
    bars4 = plt.bar(r5, y4,hatch=hatchs[3] ,facecolor='none',edgecolor=colors[3],width=bar_width, label=tree_name[3])

    # 设置x轴的标签
    plt.xticks([r + bar_width*2.5 for r in r1], xlabels,fontsize=18)
    plt.yticks([0,2000,4000],fontsize=16)

    # 设置图像的标题和坐标轴标签
    # plt.title(r"Breakdown of B$^{+}$Tree on SSD-based Systems")
    plt.ylabel(' Write Bytes Per Op',fontsize=16)
    # plt.xlabel('Node Size')
    plt.grid(axis='y', linestyle='dashdot')

    # 添加图例
    plt.legend(loc="upper left",bbox_to_anchor=(-0.16212013,1.42),ncol=2,fontsize=14)
    fname="./test/result/pic/"
    plt.savefig(fname+"figures-3-wa.pdf",format='pdf',bbox_inches='tight',dpi=900)


def plot_fifo_ratio():
    """
    different fifo size
    """
    fig=plt.figure(figsize=(4.20, 2.40),dpi=900)
    ax1= fig.add_subplot(111)
    # xlabels=["2MiB","4MiB","6MiB","8MiB","10MiB","12MiB","14MiB"]
    xlabels=["1/13","2/12","4/10","6/8","8/6","10/4","12/2"]
    # colors=['#cdd8ea','#fce49e']    # colors=['#d7c8af','#546170']     # colors=['steelblue','goldenrod']
    colors=['#112D4E','steelblue','goldenrod','#DD5746','#135D66','#704264']
    hatchs=['xx','///','-\\','++']

    x = np.arange(len(xlabels))
    r1=x
    # zbtree iops
    y1 = [389.89,404.36,387.02,385.92,400.58,458.73,382.20]
    # load 614.24,660.72,659.25,681.23,672.13,491.14,661.52
    # 创建柱状图
    bar_width = 0.35
    begin=2
    r2=[i+bar_width for i in x]
    bars1=ax1.bar(r1[begin:], y1[begin:],width=bar_width, hatch="xx",facecolor='none',edgecolor=colors[0],label="ztree-WA")
    
    ax1.grid(axis='y', linestyle='--')
    plt.xticks([i for i in x][begin:], xlabels[begin:],fontsize=14,rotation=10)
    plt.xlabel("The ratio of write sequencer and read cache",fontsize=16)
    plt.yticks([200,300,400,500],fontsize=14)
    plt.ylabel("IOPS (Kops/s)",fontsize=16)
    plt.grid(axis='y', linestyle='--')
    plt.ylim(200,500)
    # plt.legend(loc="upper left",bbox_to_anchor=(-0.04,1.25),ncol=2,frameon = False)
    fname="./test/result/pic/"
    plt.savefig(fname+"figures-10-fifo.pdf",format="pdf",bbox_inches='tight',dpi=900)

def plot_buffer_tree_size():
    """
    different node size
    """
    fig=plt.figure(figsize=(4.20, 2.40),dpi=900)
    ax1= fig.add_subplot(111)
    xlabels=["2","8","16","24","32","40","48","56","64"]
    # colors=['#cdd8ea','#fce49e']    # colors=['#d7c8af','#546170']     # colors=['steelblue','goldenrod']
    colors=['steelblue','goldenrod','#DD5746','#135D66','#704264']
    hatchs=['xx','///','-\\','++']

    x = np.arange(len(xlabels))
    r1=x
    # zbtree iops
    z_load = [764.31,1885.08,2455.93,2740.30,2904.44,3579.77,4048.21,3843.03,4834.00]
    z_run = [257.12,913.27,1445.41,1847.68,2029.27,2174.48,2027.69,2160.60,2092.64]
    # 创建柱状图
    bar_width = 0.35
    plt.plot([i+bar_width/2 for i in x], z_load, ls='dashdot',marker='D',color=colors[4],label='ztree')
    # 绘制一条平行于x轴的虚线
    plt.axhline(y=5550.88, color='r', linestyle='--',label='DRAM-B+Tree',linewidth=4)

    ax1.grid(axis='y', linestyle='--')
    plt.xticks([i+bar_width/2 for i in x], xlabels,fontsize=15)
    plt.yticks([0,2000,4000,6000],fontsize=16)
    plt.xlabel("The merged filter size (MB)",fontsize=16)
    plt.ylabel("IOPS (Kops/s)",fontsize=16)
    plt.ylim(000,6000)
    plt.grid(axis='y', linestyle='--')
    plt.legend(loc="lower right",bbox_to_anchor=(1.02,-0.03),ncol=1,fontsize=15)
    fname="./test/result/pic/"
    plt.savefig(fname+"figures-11-buffer-size.pdf",format="pdf",bbox_inches='tight',dpi=900)


def plot_varlen():
    """
    different node size
    """
    fig=plt.figure(figsize=(4.20, 2.40),dpi=900)
    ax1= fig.add_subplot(111)
    xlabels=["8","16","32","64","128","256","512"]
    # colors=['#cdd8ea','#fce49e']    # colors=['#d7c8af','#546170']     # colors=['steelblue','goldenrod']
    colors=['#135D66','#704264','steelblue','goldenrod','#DD5746']
    hatchs=['xx','///','-\\','++']

    x = np.arange(len(xlabels))
    r1=x
    load = [701.78,707.20,697.68,672.51,661.77,599.77,520.77]
    read = [223.23,217.56,211.19,209.69,208.34,210.83,210.91]
    scan = [79.16,71.82,68.63,62.30,60.99,55.26,53.04]
    bar_width = 0.35
    ax1.grid(axis='y', linestyle='--')
    # ax1.set_ylim(0,5000)
    ax1.set_ylim(0,800)
    plt.xticks([i+bar_width/2 for i in x], xlabels,fontsize=16)
    plt.yticks(fontsize=14)
    plt.xlabel("Value Length (Bytes)",fontsize=16)
    plt.ylabel("IOPS (Kops/s)",fontsize=16)
    # 创建第二个y轴
    # 创建折线图
    lines1 = ax1.plot([i+bar_width/2 for i in x], load, ls='-',marker='v',markersize=9,color=colors[0],label='Insert')
    lines1 = ax1.plot([i+bar_width/2 for i in x], read, ls='-',marker='s',markersize=9,color=colors[1],label='Read')
    lines1 = ax1.plot([i+bar_width/2 for i in x], scan, ls='-',marker='8',markersize=9,color=colors[2],label='Scan')
    # 设置第二个y轴的刻度范围

    # 添加图例
    plt.legend(loc="upper left",bbox_to_anchor=(-0.02,0.75),ncol=2,fontsize=14)
    fname="./test/result/pic/"
    plt.savefig(fname+"figures-12-varlen.pdf",format="pdf",bbox_inches='tight',dpi=900)


# %%
def transfer_data(data):
    for i in range(len(data)):
        data[i] = np.log10(data[i])
    print(data)
def plot_latency(mode="write"):
    """
    Evaluation: latency
    """

    conv_btree = [244192,  931054, 3197419, 5061856, 7121156,11540515]
    transfer_data(conv_btree)
    f2fs_btree = [416164, 1104818, 3226669, 5276066, 9585879,97776240]
    transfer_data(f2fs_btree)
    cow_btree=[304198, 1652658, 5892735,12769172,41134756,74875784]
    transfer_data(cow_btree)
    # zbtree=   [556,3042,96137,192861,97439808,1947400704,3136155648]
    zbtree=     [1240,    3722,  167670,19674528,29540940,73677024]
    transfer_data(zbtree)

    if mode=="read":
        conv_btree=[179583,  464547,  926347, 2358787, 3228582, 6136853]
        f2fs_btree=[145107,  357730,  675655, 1049466, 1928182, 947772]
        cow_btree=[115526,  209058,  773067, 1839328, 2741558, 4346126]
        zbtree=[116926,  186322,  292011,  398962,  503070, 4551989]
        transfer_data(conv_btree),transfer_data(f2fs_btree),transfer_data(cow_btree),transfer_data(zbtree)

    y=[conv_btree,f2fs_btree,cow_btree,zbtree]

    xlabels=["50%","90%","99%","99.9%","99.99%","100%"]
    latency_y = ['', '10ns', '', '1us', '10us', '100us', '1ms','10ms','100ms','1s']
    colors=['#135D66','steelblue','goldenrod','#944E63','#ce383e','#f7bf8a','#b3b3b3','#DD5746','#704264']
    tree_name=['conv_btree','f2fs_btree','cow_btree','ztree']
    hatchs=['--','////','\\\\','xx','..','o\\o\\']

    fig=plt.figure(figsize=(4.20, 2.40),dpi=900)
    ax1= fig.add_subplot(111)
    x = np.arange(len(xlabels))
    bar_width = 1.0/(len(xlabels))


    # 
    begin=1
    pos=5
    index0=np.arange(len(xlabels))
    for i,tree_data in enumerate(y):
        index=index0+i*bar_width
        plt.bar(index[begin:pos], height=tree_data[begin:pos],facecolor='none',width=bar_width, ls='-',lw=1,edgecolor=colors[i],hatch=hatchs[i],label=tree_name[i])
    y_title="Write latency"
    if mode=="read":
        y_title="Read latency"
    # plt.title(y_title,size=10)
    plt.ylabel(y_title,size=16)
    plt.grid(axis='y', linestyle='--')
    plt.xticks(np.arange(len(xlabels))[begin:pos]+bar_width*1.5, xlabels[begin:pos], size=17,rotation=10)

    plt.yticks(range(len(latency_y)),latency_y,size=16)
    # 添加图例
    if mode=="read":
        plt.ylim(4.8,7)
        plt.legend(loc="upper left",bbox_to_anchor=(-0.16,1.42),ncol=2,fontsize=14)
    elif mode=="write":
        plt.ylim(2.9,8)
        plt.legend(loc="upper left",bbox_to_anchor=(-0.16,1.42),ncol=2,fontsize=14)
        
    # plt.legend(loc="upper left",bbox_to_anchor=(-0.01,1.02))
    fname="./test/result/pic/"
    plt.savefig(fname+f"figures-6-{mode}-latency-new.pdf",format='pdf',bbox_inches='tight',dpi=900)


def plot_app_latency_imp(tree_datas,ptitle):
    """
    Evaluation: wiredtiger and rocksdb latency
    """

    rocksdb,wiredtiger,ztree = tree_datas
    transfer_data(rocksdb),transfer_data(wiredtiger),transfer_data(ztree)
    y=[rocksdb ,wiredtiger,ztree]

    xlabels=["avg","min","50%","90%","99%","99.9%","99.99%","100%"]
    latency_y = ['', '10ns', '', '1us', '10us', '100us', '1ms','10ms','100ms','1s']
    colors=['#135D66','steelblue','goldenrod','#944E63','#ce383e','#f7bf8a','#b3b3b3','#DD5746','#704264']
    tree_name=['LSM-Tree',r'B$^{+}$Tree',r'Z$^{+}$Tree']
    hatchs=['--','////','\\\\','xx','..','o\\o\\']
    markers=['v','d','p','8','*','>','<']

    fig=plt.figure(figsize=(4.20, 2.40),dpi=900)
    ax1= fig.add_subplot(111)
    x = np.arange(len(xlabels))
    bar_width = 1.0/(len(xlabels)-2)

    # 
    base=2
    pos=7
    index0=np.arange(len(xlabels))
    for i,tree_data in enumerate(y):
        # index=index0+i*bar_width
        # plt.bar(index[:pos], height=tree_data[:pos],facecolor='none',width=bar_width, ls='-',lw=1,edgecolor=colors[i],hatch=hatchs[i],label=tree_name[i])
        plt.plot(index0[base:pos],tree_data[base:pos],marker=markers[i],markersize=12,linestyle="-.",color=colors[i],label=tree_name[i])
    y_title=f"{ptitle}"[2:]
    y_title="Tail latency"
    # if ptitle.find("Write")!=-1:
        # y_title="Write latency"
    # elif ptitle.find("Read")!=-1:
        # y_title="Read latency"

    plt.ylabel(y_title,size=18)
    plt.grid(axis='y', linestyle='--')
    plt.xticks(np.arange(len(xlabels))[base:pos], xlabels[base:pos], size=14,rotation=20)
    plt.yticks(range(len(latency_y)),latency_y,size=14)
    plt.ylim(2.8,8.3)

    # if ptitle=="Write latency":
    plt.legend(loc="upper left",bbox_to_anchor=(-0.36,1.24),fontsize=13,ncol=3)
    # elif ptitle=="Read latency":
    # else:
        # plt.legend(loc="lower right",bbox_to_anchor=(1.01,-.02),fontsize=16)
    
    
    fname=f"./test/result/pic/"
    ptitle=ptitle.replace(" ","-")
    plt.savefig(fname+f"figures-13-app-{ptitle}.pdf",format='pdf',bbox_inches='tight',dpi=900)

def plot_app_latency():

    rocksdb_write = [332212,     791,  213885,  575709, 2098842, 6579512, 8684927,12597550]
    wiredtiger_write = [162749,     763,  141666,  284756,  456655,  627696,  831480, 4240542]
    zbtree_write = [112908,     148,  117835,  187042,  293248,  400978,  510487, 3188438]
    # plot_app_latency_imp([rocksdb_write,wiredtiger_write,zbtree_write],"1-RD Lat ReadOnly")


    rocksdb_write = [531564,    1033,  474468,  784679, 1545229, 5275880, 8016306,12746652]
    wiredtiger_write = [337174,     936,  127979,  723477, 2385684, 5900536,117618128,171539296]
    zbtree_write = [190354,     186,  122650,  336614, 1466082, 2925006, 5618085,14318438]
    # plot_app_latency_imp([rocksdb_write,wiredtiger_write,zbtree_write],"2-RD Lat ReadHeavy")

    rocksdb_write = [1476507,    1639, 1306938, 2607368, 4479394, 6072549, 7522926, 9982876]
    wiredtiger_write = [323503,    1546,  161880,  670024, 2650193, 4691199,36682232,204735312]
    zbtree_write = [201966,347,120728,348990,1734158,4169384,6949890,22652210]
    # plot_app_latency_imp([rocksdb_write,wiredtiger_write,zbtree_write],"3-RD Lat WriteHeavy")


    rocksdb_write = [157975,    2362,   39282,   51474, 2291549, 3363829, 4575727,55846384]
    wiredtiger_write = [834919,    2573,  288685, 2236333, 4754532, 6406550, 7599131,80431408]
    zbtree_write = [68785,     149,    1240,    3722,  167670,19674528,29540940,73677024]
    # plot_app_latency_imp([rocksdb_write,wiredtiger_write,zbtree_write],"4-WR Lat WriteOnly")

    rocksdb_write = [ 4465,    1448,    4079,    5678,    8955,   15790,  538481,  885788]
    wiredtiger_write = [323503,    1546,  161880,  670024, 2650193, 4691199,36682232,204735312]
    zbtree_write = [8651,     215,    1245,    1785,  110509,11032231,23059504,54757964]
    # plot_app_latency_imp([rocksdb_write,wiredtiger_write,zbtree_write],"5-WR Lat ReadHeavy")


    rocksdb_write = [6282,1080,4581,9601,13600,301260,2197400,2227910]
    wiredtiger_write = [748038,    2008,  297897, 1768342, 3851718,21847534,129294384,26932432]
    zbtree_write = [177726,123,1144,1826,108293,6605660,57089172,127313192]
    plot_app_latency_imp([rocksdb_write,wiredtiger_write,zbtree_write],"6-WR Lat WriteHeavy")


# %%
def plot_breakdown():
    """
    Evaluation: breakdown of each methods
    """
    plt.figure(figsize=(4.20, 2.40),dpi=900)
    
    bar_width = 0.2
    xlabels=["Write","Read"]
    r1 = np.arange(len(xlabels))
    r2 = [x + bar_width for x in r1]
    r3 = [x + bar_width for x in r2]
    r4=[x + bar_width for x in r3]
    r5=[x + bar_width for x in r4]
    y0=[83.73,265.79]
    y1=[104.31,411.16]
    y2=[387.31,456.98] 
    y3=[623.61,457.35] 
    # colors=['#393646','#333333','#4F4557','#393646']
    colors=['goldenrod','steelblue','#135D66','#704264','#DD5746']
    # colors=["#F9F7F7","#DBE2EF","#3F72AF","#112D4E"]
    # colors=['#393646','#666666','#333333','#1A1A1A']
    hatchs=['--','///','-\\','xx','++']

    tree_name=['WriteSequencer','+ReadCache','+MergedFilter','+BatchMerge']
    # bars1 = plt.bar(r1, y0,hatch=hatchs[0],facecolor='none',edgecolor=colors[0], width=bar_width,label=tree_name[0])
    bars1 = plt.bar(r2, y0,hatch=hatchs[0],facecolor='none',edgecolor=colors[0], width=bar_width,label=tree_name[0])
    bars2 = plt.bar(r3, y1,hatch=hatchs[1] ,facecolor='none',edgecolor=colors[1],width=bar_width,label=tree_name[1])
    bars3 = plt.bar(r4, y2,hatch=hatchs[2] ,facecolor='none',edgecolor=colors[2],width=bar_width, label=tree_name[2])
    bars4 = plt.bar(r5, y3,hatch=hatchs[3] ,facecolor='none',edgecolor=colors[3],width=bar_width, label=tree_name[3])

    # 设置x轴的标签
    plt.xticks([r + bar_width*2.5 for r in r1], xlabels,fontsize=16)
    plt.ylabel('IOPS (Kops/s)',fontsize=16)
    plt.yticks(fontsize=16)
    plt.grid(axis='y', linestyle='dashdot',linewidth=0.5)
    # 添加图例
    plt.legend(loc="upper left",bbox_to_anchor=(-0.3,1.41),ncol=2,fontsize=14)
    fname="./test/result/pic/"
    plt.savefig(fname+"figures-5-breakdown.pdf",format='pdf',bbox_inches='tight',dpi=900)
# %%


def plot_skewed():
    """
    
    """
    fig=plt.figure(figsize=(4.20, 2.40),dpi=900)
    ax1= fig.add_subplot(111)
    xlabels=["0.5","0.6","0.7","0.8","0.9","0.99"]
    colors=['#135D66','#704264']     # colors=['steelblue','goldenrod'] colors=['#cdd8ea','#fce49e'] 
    # colors=['#135D66','#704264','steelblue','goldenrod','#DD5746']
    colors.reverse()
    x = np.arange(len(xlabels))
    # zbtree iops
    ZB_IOPS=[365.69,468.80,564.22,695.19,743.98,1487.98]
    ZB_HIT_RATIO = [11.44,12.45,14.32,17.64,23.70,35.79]
    # f2fs write bytes per op
    F2_IOPS = [140.81,141.81,145.58,153.58,178.36,240.24]
    # f2fs iops
    F2_HIT_RATIO = [845.77,211.19,134.17,107.48,94.37, 88.31, 83.79]
    COW_IOPS=[128.95,136.95,127.00,127.25,143.45,193.4]
    CONV_IOPS=[154.87,154.90,155.64,161.50,180.19,227.9]
    markers=['v','d','p','8','*','>','<']
    colors=['#704264','#135D66','#112D4E','#393646','goldenrod','#DD5746','#4F4557']

    ax1.grid(axis='y', linestyle='--')
    lines1 = ax1.plot(x, ZB_IOPS, ls='dashed',marker='d',markersize=11,color=colors[0],label='ztree')
    lines2 = ax1.plot(x, F2_IOPS, ls='dashed',marker='X',markersize=10,color=colors[1],label='f2fs_btree')
    # lines2 = ax1.plot(x, COW_IOPS, ls='dashed',marker='+',markersize=10,color=colors[2],label='cow_btree')
    # lines2 = ax1.plot(x, CONV_IOPS, ls='dashed',marker='v',markersize=10,color=colors[3],label='conv_btree')
    plt.yticks(fontsize=14)
    ax1.set_ylim(-0,ceil_to_n(max(ZB_IOPS), 100)+100)
    ax1.set_yticks([0,500,1000,1500])
    ax1.set_ylabel("IOPS (Kops/s)",fontsize=16)
    ax1.set_xticks(x,xlabels,fontsize=16)
    ax1.set_xlabel("Skew factor",fontsize=16)

    # 添加图例
    plt.legend(loc="upper left",bbox_to_anchor=(-0.018,1.05),ncol=1,fontsize=16)
    fname="./test/result/pic/"
    plt.savefig(fname+"figures-8-skewed-factor.pdf",format="pdf",bbox_inches='tight',dpi=900)


def plot_node():
    """
    different node size
    """
    fig=plt.figure(figsize=(4.20, 2.40),dpi=900)
    ax1= fig.add_subplot(111)
    xlabels=["4KB","8KB","16KB","32KB"]
    # colors=['#cdd8ea','#fce49e']    # colors=['#d7c8af','#546170']     # colors=['steelblue','goldenrod']
    colors=['#135D66','#704264','steelblue','goldenrod','#DD5746']
    hatchs=['xx','///','-\\','++']

    x = np.arange(len(xlabels))
    r1=x
    y1 = [548.48,877.06,2160.77,3599.36]
    # zbtree iops
    y2 = [286.64,462.38,438.62,132.10]
    # f2fs write bytes per op
    y3 = [7189.12,10731.01,17841.41,32074.50]
    # f2fs iops
    y4 = [86.23,65.96,38.20,20.18]

    # 创建柱状图
    bar_width = 0.35
    r2=[i+bar_width for i in x]
    bars1=ax1.bar(r1, y1,width=bar_width, hatch="xx",facecolor='none',edgecolor=colors[1],label="ztree-WA")
    bars2=ax1.bar(r2, y3,width=bar_width, hatch="++",facecolor='none',edgecolor=colors[0],label="f2fs_btree-WA")

    plt.legend(loc="upper left",bbox_to_anchor=(-0.2,1.25),ncol=2,frameon = False,fontsize=16)
    
    ax1.grid(axis='y', linestyle='--',linewidth=0.7)
    # ax1.set_ylim(0,5000)
    plt.xticks([i+bar_width/2 for i in x], xlabels,fontsize=16)
    plt.xlabel("Leaf node size",fontsize=16)
    plt.ylabel("Write Bytes Per Op",fontsize=16) 
    plt.yticks([0,4096,8192,16384,32768],['0','4KB','8KB','16KB','32KB'],fontsize=16)
    # plt.ylim([-1000,33000])
    # 创建第二个y轴
    ax2=plt.twinx()
    # 创建折线图
    lines1 = ax2.plot([i+bar_width/6 for i in x], y2, ls='dashdot',marker='D',markersize=10,color=colors[1],label='ztree-IOPS')
    lines2 = ax2.plot([i+bar_width/6 for i in x], y4, ls='dashdot',marker='X',markersize=10,color=colors[0],label='f2fs_btree-IOPS')
    # 设置第二个y轴的刻度范围
    ax2.set_ylim(-80,610)
    plt.yticks(fontsize=16)
    ax2.set_yticks([0,200,400,600])
    ax2.set_ylabel("KIOPS/s",fontsize=16)
    # ax2.set_ylim(-50,610)
    # 添加图例
    # ax1.legend(handles=[bars1,bars2,lines1,lines2],loc="upper left",bbox_to_anchor=(-0.01,1.02),frameon = False)
    plt.legend(loc="upper left",bbox_to_anchor=(-0.25,1.4),ncol=2,frameon = False,fontsize=16)
    fname="./test/result/pic/"
    plt.savefig(fname+"figures-9-node.pdf",format="pdf",bbox_inches='tight',dpi=900)


def plot_size():
    """
    different size
    """
    fig=plt.figure(figsize=(4.20, 2.40),dpi=900)
    ax1= fig.add_subplot(111)
    xlabels=["2M","4M","8M","16M","32M","64M"]
    # colors=['#cdd8ea','#fce49e']    # colors=['#d7c8af','#546170']     # colors=['steelblue','goldenrod']
    colors=['#135D66','#704264','steelblue','goldenrod','#DD5746']
    hatchs=['xx','///','-\\','++']

    x = np.arange(len(xlabels))
    r1=x
    y1 = [194.56,333.18,598.72,1057.44,1682.18,2347.98]
    # zbtree iops
    y2 = [1922.02,935.75,503.80,280.73,175.30,157.30]
    # f2fs write bytes per op
    y3 = [2179.33,4164.74,5662.21,6775.36,7698.91, 8348.96]
    # f2fs iops
    y4 = [216.12,131.76,106.13,94.98,89.76,82.62]

    # 创建柱状图
    bar_width = 0.35
    r2=[i+bar_width for i in x]
    bars1=ax1.bar(r1, y1,width=bar_width, hatch="xx",facecolor='none',edgecolor=colors[1],label="ztree-WA")
    bars2=ax1.bar(r2, y3,width=bar_width, hatch="++",facecolor='none',edgecolor=colors[0],label="f2fs_btree-WA")
    plt.legend(loc="upper left",bbox_to_anchor=(-0.2,1.25),ncol=2,frameon = False,fontsize=16)
    
    ax1.grid(axis='y', linestyle='--')
    # ax1.set_ylim(0,5000)
    plt.xticks([i+bar_width/2 for i in x], xlabels,fontsize=16)
    plt.xlabel("# of inserted KV pairs",fontsize=16)
    plt.ylabel("Write Bytes Per Op",fontsize=16)
    # plt.ylim(-80,8400)
    plt.yticks([0,2000,4000,6000,8000],fontsize=16)
    # 创建第二个y轴
    ax2=plt.twinx()
    # 创建折线图
    lines1 = ax2.plot([i+bar_width/6 for i in x], y2, ls='dashdot',marker='D',markersize=8,color=colors[1],label='ztree-IOPS')
    lines2 = ax2.plot([i+bar_width/6 for i in x], y4, ls='dashdot',marker='X',markersize=8,color=colors[0],label='f2fs_btree-IOPS')
    # 设置第二个y轴的刻度范围
    plt.yticks(fontsize=16)
    ax2.set_ylim(-10,1100)# ceil_to_n(max(y2), 1000))
    ax2.set_yticks([0,250,500,750,1000])
    ax2.set_ylabel("KIOPS/s",fontsize=16)
    p1=[x[0]+bar_width/2, 1080]
    plt.scatter(p1[0],p1[1] ,marker='D',color=colors[1])
    # 添加文字
    plt.text(p1[0]+0.1, p1[1]-60,"1922.08", fontsize=14,ha='center',va='top')    
    # ax2.annotate('2978.4', xy=(p1[0], p1[1]), xytext=(p1[0], p1[1]+100))


    # 添加图例
    # ax1.legend(handles=[bars1,bars2,lines1,lines2],loc="upper left",bbox_to_anchor=(-0.01,1.02),frameon = False)
    plt.legend(loc="upper left",bbox_to_anchor=(-0.25,1.4),ncol=2,frameon = False,fontsize=16)
    fname="./test/result/pic/"
    plt.savefig(fname+"figures-4-size.pdf",format="pdf",bbox_inches='tight',dpi=900)


# %%
def plot_app():
    """
    Evaluation: comparsion with app
    """
    plt.figure(figsize=(7.20, 2.40),dpi=900)
    
    bar_width = 0.25
    # xlabels=["load","amzn","osm","wiki","facebook"]
    xlabels=["ycsba","ycsbb","ycsbc","ycsbe","amzn","wiki","facebook"]
    r1 = np.arange(len(xlabels))
    # zbtree=[507.54,214.83,267.30,400.03,384.76,382.34,378.33] 
    zbtree=[435.88,342.22,388.64,247.48,300.91,302.86,298.33] 
    # wtiger=[90.69,68.72,154.58,212.67,352.14,316.43,307.09]
    wtiger=[65.20,64.60,142.54,213.50,287.97,330.31,308.74]
    # rocks=[460.08,133.40,119.78,9,143.63,163.34,161.30] 
    rocks=[587.59,141.84,78.62,9,65.34,139.84,168.12] 
    # colors=[,'#333333',,]
    colors=['#112D4E','#135D66','#704264','#393646','goldenrod','#DD5746','#4F4557']
    # colors=['#393646','#666666','#333333',]
    hatchs=['++','///','xx','-\\']

    trees=[wtiger,rocks,zbtree]
    tree_name=[r'B$^{+}$Tree','LSM-Tree',r'Z$^{+}$Tree']
    for i,tree in enumerate(tree_name):
        plt.bar([x+bar_width*i for x in r1], trees[i], hatch=hatchs[i],facecolor='none',edgecolor=colors[i], width=bar_width,label=tree) 

    # 设置x轴的标签
    plt.xticks([r + bar_width*1.1 for r in r1], xlabels,fontsize=16,rotation=9)
    plt.ylim(0,ceil_to_n(max(rocks),50))
    plt.yticks([0,200,400,600],fontsize=14)
    plt.yticks(fontsize=14)

    # 设置图像的标题和坐标轴标签
    plt.ylabel('IOPS (Kops/s)',fontsize=14)
    plt.grid(axis='y', linestyle='dashdot')

    # 添加图例
    plt.legend(loc="upper left",bbox_to_anchor=(0.115,1.03),ncol=3,fontsize=14)
    fname="./test/result/pic/"
    plt.savefig(fname+"figures-7-app.pdf",format='pdf',bbox_inches='tight',dpi=900)

    
def app_motivate():
    bar_width = 0.25
    # xlabels=["load","amzn","osm","wiki","facebook"]
    xlabels=["w10","w8r2","w5r5","w2r8","r10"]
    r1 = np.arange(len(xlabels))
    zbtree=[507.54,214.83,291.59,267.30,400.03] 
    wtiger=[90.69,68.72,107.90,154.58,341.10]
    rocks=[460.08,133.40,142.34,119.78,179.29] 
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
    plt.xticks([r + bar_width*1 for r in r1], xlabels,fontsize=18,rotation=10)
    # plt.ylim(0,ceil_to_n(max(zbtree),50))
    plt.yticks([0,200,400],fontsize=14)

    # 设置图像的标题和坐标轴标签
    plt.ylabel('IOPS (Kops/s)',fontsize=18)
    plt.grid(axis='y', linestyle='dashdot')

    # 添加图例
    plt.legend(loc="upper left",bbox_to_anchor=(0.16,1.05),ncol=1,fontsize=18)
    fname="./test/result/pic/"
    plt.savefig(fname+"figures-7-app-motivate.pdf",format='pdf',bbox_inches='tight',dpi=900)

    bar_width = 0.25
    # xlabels=["load","amzn","osm","wiki","facebook"]
    xlabels=["w10","w8r2","w5r5","w2r8","r10"]
    r1 = np.arange(len(xlabels))
    zbtree=[507.54,214.83,291.59,267.30,400.03] 
    wtiger=[90.69,68.72,107.90,154.58,341.10]
    rocks=[460.08,133.40,142.34,119.78,179.29] 
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
    plt.xticks([r + bar_width*1 for r in r1], xlabels,fontsize=18,rotation=10)
    # plt.ylim(0,ceil_to_n(max(zbtree),50))
    plt.yticks([0,200,400],fontsize=14)

    # 设置图像的标题和坐标轴标签
    plt.ylabel('IOPS (Kops/s)',fontsize=18)
    plt.grid(axis='y', linestyle='dashdot')

    # 添加图例
    plt.legend(loc="upper left",bbox_to_anchor=(0.16,1.05),ncol=1,fontsize=18)
    fname="./test/result/pic/"
    plt.savefig(fname+"figures-7-app-motivate.pdf",format='pdf',bbox_inches='tight',dpi=900)

# %%
if __name__ == '__main__':
    # plot_micro()
    # plot_latency("write")
    # plot_latency("read")
    # plot_wa_sensi()
    # plot_breakdown()
    # plot_skewed()
    # plot_fifo_ratio()
    # plot_buffer_tree_size()
    plot_node()
    plot_size()
    # plot_varlen()
    # plot_app()
    plot_app_latency()