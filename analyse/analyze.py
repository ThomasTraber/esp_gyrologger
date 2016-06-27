#!/usr/bin/python
#vim: fileencoding=utf8
"""
Analyze Gyrologger data
"""

global interactive
from pylab import *
import sys
import os.path
#from IPython.Shell import IPShellEmbed
#ipshell= IPShellEmbed() 
from IPython import embed as ipshell
import scipy.signal as ss

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
    figure("sampling hist")
    title(basename)
    hist(dtimes,4,alpha=0.3,log=True)
    grid(1)
    xlabel("Sampling Intervals / s")
    ylim(1,len(dtimes))
    twinx()
    hist(dtimes,400,log=True,normed=False)
    ylim(1,len(dtimes))
    savefig("%s_hist.png"%basename)

    figure("sampling intervals")
    clf()
    title(basename)
    grid(1)
    plot(times[1:],dtimes,".")
    xlabel("Time / s")
    ylabel("Samplint Interval / s")
    savefig("%s_samplinginterval.png"%basename)

def plot_data(data):
    times,gx,gy,gz,ax,ay,az,basename = data
    figure("data")
    clf()
    grid(1)
    title(basename)
    plot(times,gx,label="gx")
    plot(times,gy,label="gy")
    plot(times,gz,label="gz")
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

    figure("sum")
    clf()
    title("Rotation Angle from Gyrosum and Total Acceleration")
    grid(1)
    asum = sqrt(ax*ax+ay*ay+az*az)
    dtimes = gradient(times)
    gxpos = cumsum(gx*dtimes)
    gypos = cumsum(gy*dtimes)
    gzpos = cumsum(gz*dtimes)

    axpos = arctan2(ay,az)*180.0/pi
    aypos = arctan2(ax,az)*180.0/pi
    azpos = arctan2(ax,ay)*180.0/pi

    plot(times,gxpos,"b",label="gxpos")
    plot(times,gypos,"g",label="gypos")
    plot(times,gzpos,"r",label="gzpos")

    plot(times,axpos,"b",ls=":",label="axpos")
    plot(times,aypos,"g",ls=":",label="aypos")
    plot(times,azpos,"r",ls=":",label="azpos")

    legend(loc=2)
    ylabel("GyroSum")
    xlabel("Time / s")
    twinx()
    plot(times,asum,label="asum")
    legend(loc=1)
    ylabel("AccVectorLen")
    savefig("%s_sumcalc.png"%basename)

    figure("sumdetrended")
    clf()
    title("Rotation Angle from Detrended Gyrosum and Total Acceleration")
    grid(1)
    axpos,aypos = ss.detrend(-aypos),ss.detrend(axpos)
    azpos = ss.detrend(azpos)
    gxpos = ss.detrend(gxpos)
    gypos = ss.detrend(gypos)
    gzpos = ss.detrend(gzpos)
    plot(times,gxpos,"b",label="gxpos")
    plot(times,gypos,"g",label="gypos")
    plot(times,gzpos,"r",label="gzpos")

    plot(times,axpos,"b",ls=":",label="axpos")
    plot(times,aypos,"g",ls=":",label="aypos")
    plot(times,azpos,"r",ls=":",label="azpos")
    legend(loc=2)
    ylabel("Rotation Angle / deg")
    xlabel("Time / s")
    twinx()
    plot(times,asum,"k",alpha=0.5,label="asum")
    legend(loc=1)
    ylabel("Total Acceleration / g")
    savefig("%s_sumdetrended.png"%basename)

def analyze(filename):
    data = load(filename)
    plot_samplinginterval_hgram(data)
    plot_data(data)
    if interactive:
        print "starting ipshell"
        ipshell()

interactive = False

if __name__=="__main__":
    import sys
    for arg in sys.argv[1:]:
        print arg
        if arg == "-i":
            print "interactive"
            interactive=True
            ion()
            break;
    analyze(sys.argv[-1])

