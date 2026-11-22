# Time common Python operations on ELKS.
#
#   ICOUNT=1 tools/qemu-run.sh tools/bench.py    reproducible timings in QEMU
#   micropython bench.py                         on real hardware
#
# Prints the time of each section in milliseconds (the clock ticks every
# 10 ms).  Under ICOUNT=1 the times are about 30 to 35 times shorter than on a
# 4.77 MHz 8088.
import time

N = 10000
t0 = 0

def start():
    global t0
    t0 = time.ticks_ms()

def done(name, count):
    ms = time.ticks_diff(time.ticks_ms(), t0)
    print("%6d ms  %s (x%d)" % (ms, name, count))

start()
for i in range(N):
    pass
done("1 empty for loop, globals", N)

t = 0
start()
for i in range(N):
    t += i
done("2 add only, globals", N)

start()
for i in range(N):
    i % 2
done("3 modulo only, globals", N)

t = 0
start()
for i in range(N):
    if i % 2:
        t += i
done("4 if/modulo/add, globals", N)

def f(n):
    t = 0
    for i in range(n):
        if i % 2:
            t += i
    return t

start()
f(N)
done("5 if/modulo/add, in a function", N)

def g(n):
    for i in range(n):
        pass

start()
g(N)
done("6 empty for loop, in a function", N)

def h(n):
    i = 0
    while i < n:
        i += 1
    return i

start()
h(N)
done("7 while loop, in a function", N)

def k():
    pass

def calls(n):
    for i in range(n):
        k()

start()
calls(N)
done("8 function call, in a function", N)

start()
for i in range(100):
    x = 2 ** 0.5
done("9 2**0.5", 100)

y = 1 / 3
start()
for i in range(100):
    s = str(y)
done("10 str(1/3)", 100)

start()
for i in range(1000):
    y = i / 4 + 1.5
done("11 float divide and add", 1000)

class C:
    def __init__(self):
        self.x = 0
    def m(self, a):
        return a

def meth(n):
    c = C()
    for i in range(n):
        c.m(i)
start()
meth(N // 5)
done("13 method call", N // 5)

def attr(n):
    c = C()
    for i in range(n):
        c.x = c.x + 1
start()
attr(N // 5)
done("14 attribute load and store", N // 5)

def lst(n):
    l = []
    for i in range(n):
        l.append(i)
    t = 0
    for i in range(n):
        t += l[i]
    for v in l:
        t += v
start()
lst(N // 10)
done("15 list append, index, iterate", N // 10)

def dct(n):
    d = {}
    for i in range(n):
        d[i & 63] = i
    for i in range(n):
        d["k"] = d.get("k", 0) + 1
start()
dct(N // 10)
done("16 dict set and get", N // 10)

def strs(n):
    for i in range(n):
        s = "a" + str(i)
        s = "%d:%s" % (i, s)
        len(s)
start()
strs(N // 50)
done("17 str build and format", N // 50)

def glb(n):
    for i in range(n):
        len
        N
start()
glb(N)
done("18 load builtin and global, in a function", N)

def arith(n):
    t = 0
    for i in range(n):
        t = (i * 7 + t) % 1000
        t += i // 3
    return t
start()
arith(N // 2)
done("19 multiply, modulo, floor divide", N // 2)

def big(n):
    t = 0
    for i in range(n):
        t = (i * 70000 + t) % 100003
        t += (i + 100000) // 3
    return t
start()
big(N // 10)
done("20 same with values over 16 bits", N // 10)
