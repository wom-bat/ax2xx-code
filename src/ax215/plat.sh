#!/bin/sh
# Try to distinguish i.mx6 (sabre lite) from odroid XU3 from Novena
#
grep > /dev/null 2>&1 ttymxc /proc/devices && { echo SABRELITE; exit 0;}


grep > /dev/null 2>&1 exynos /proc/cpuinfo && {echo ODROIDXU3; exit 0;}

echo NOVENA
