import time
from ringnes import Ringbuffer
import matplotlib.pyplot as plt

import numpy as num
BUF_CAPACITY = 4096 * 1024


def test_ringbuffer():
    data = num.arange(1022, dtype=num.int32)

    buf = Ringbuffer(BUF_CAPACITY)
    print(buf)
    print(type(buf))
    print(dir(buf))

    for _ in range(20):
        buf.put(memoryview(data))
        arr = num.frombuffer(buf, dtype=num.int32)
        print(arr)
        time.sleep(.0)

    plt.plot(arr)
    plt.show()


if __name__ == '__main__':
    test_ringbuffer()
