import time
from ringnes import Ringbuffer
import matplotlib.pyplot as plt

import numpy as num
BUF_CAPACITY = 4096 * 5


def test_ringbuffer():
    data = num.arange(842, dtype=num.int32)

    buf = Ringbuffer(BUF_CAPACITY)
    print(buf)
    print(type(buf))
    print(dir(buf))

    for _ in range(300):
        buf.put(memoryview(data))
        arr = num.frombuffer(buf, dtype=num.int32)
        print(buf.head, buf.used)
        print(arr)
        time.sleep(.0)

    plt.plot(arr)
    plt.show()


if __name__ == '__main__':
    test_ringbuffer()
