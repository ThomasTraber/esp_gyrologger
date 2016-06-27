#!/usr/bin/python
#vim: fileencoding=utf8
"""
Analyze Gyrologger data
"""

from pylab import *
import sys
import os.path

basename = ""

def load(filename):
    global basename
    gfs,afs=1,1
    ax,ay,az=None,None,None
    basename=os.path.splitext(filename)[0]
    inf = file(filename)
    for line in inf.readlines():
        if not line.startswith("#"):
            break
        words = line.strip().split()
        if words[1].startswith("GyroFullScale"):
            gfs = int(words[1].split("=")[-1])
        if words[1].startswith("AccFullScale"):
            afs = int(words[1].split("=")[-1])

    D = loadtxt(filename,comments="#",unpack=True)
    if len(D)==7:
        times,gx,gy,gz,ax,ay,az = D
    else:
        times,gx,gy,gz = D
    times *= 1e-4
    times -= times[0]
    gx = (1.0*gx*gfs)/2**15
    gy = (1.0*gy*gfs)/2**15
    gz = (1.0*gz*gfs)/2**15
    
    if ax!=None:
        ax = (1.0*ax*afs)/2**15
        ay = (1.0*ay*afs)/2**15
        az = (1.0*az*afs)/2**15
    return times,gx,gy,gz,ax,ay,az,basename

def plot_samplinginterval_hgram(data):
    times,gx,gy,gz,ax,ay,az,basename = data
    dtimes = diff(times)
    print "Max. Samplingtime",dtimes.max()
    print "Median Samplingtime",median(dtimes)
    print "Average Samplintime",average(dtimes)
    title(basename)
    hist(dtimes,4,alpha=0.3,log=True)
    grid(1)
    xlabel("Sampling Intervals / s")
    ylim(1,len(dtimes))
    twinx()
    hist(dtimes,400,log=True,normed=False)
    ylim(1,len(dtimes))
    savefig("%s_hist.png"%basename)

    clf()
    title(basename)
    grid(1)
    plot(times[1:],dtimes,".")
    xlabel("Time / s")
    ylabel("Samplint Interval / s")
    savefig("%s_samplinginterval.png"%basename)

def plot_data(data):
    times,gx,gy,gz,ax,ay,az,basename = data
    clf()
    grid(1)
    title(basename)

    plot(times,gx,".",label="gx")
    plot(times,gy,".",label="gy")
    plot(times,gz,".",label="gz")
    legend(loc=2)
    xlabel("Time / s")
    ylabel("Angular Speed")
    if (ax!=None):
        twinx()
        plot(times,ax,ls=":",label="ax")
        plot(times,ay,ls=":",label="ay")
        plot(times,az,ls=":",label="az")
        legend(loc=1)
        ylabel("Acceleration")
    savefig("%s_time.png"%basename)
    

def analyze(filename):
    data = load(filename)
    plot_samplinginterval_hgram(data)
    plot_data(data)


if __name__=="__main__":
    import sys
    analyze(sys.argv[1])

