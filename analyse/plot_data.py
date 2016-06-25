#!/usr/bin/python
#vim: fileencoding=utf8
"""
Plots the raw data from the MPU logger file
"""

from pylab import *
import sys
import os.path

noacc=False

def plotraw(filename):
    basename=os.path.splitext(filename)[0]
    D = loadtxt(filename,comments="#")
    times = D[:,0]*1e-4
    times -= times[0]
    gx = D[:,1]
    gy = D[:,2]
    gz = D[:,3]
    try:
        ax = D[:,4]
        ay = D[:,5]
        az = D[:,6]
    except:
        noacc=True
        print "No accelerator data available"

    grid(1)    
    plot(times,gx,label="gx")
    plot(times,gy,label="gy")
    plot(times,gz,label="gz")
    legend(loc=2)
    ylabel("Angular Speed")
    xlabel("Time / s")
    if not noacc:
        twinx()
        plot(times,ax,ls=":",label="ax")
        plot(times,ay,ls=":",label="ay")
        plot(times,az,ls=":",label="az")
        legend(loc=1)
        ylabel("Acceleration")
    savefig("%s.png"%basename)

if __name__=="__main__":
    plotraw(sys.argv[1])
