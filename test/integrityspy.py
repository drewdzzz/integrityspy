import json
import random
import time
import os
import signal
import shutil
import subprocess
from sys import platform

file = "../build/src/integrityspy"
report = './.integrityspy-report.json'

def launch_demon(dir, interval):
    """
    Launches demon and returns its pid.
    """
    demon = subprocess.Popen([file, '--dir', dir, '--interval', interval],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    msg = demon.stdout.readline().decode('ascii')
    if msg == "":
        errmsg = str(demon.stderr.readline())
        print('ERROR:', errmsg)
        return 0
    pid_str = msg.split(" ")[-1]
    pid = int(pid_str)
    print("Demon is launched at {}".format(pid))
    return pid

alphabet = "qwertyuiopasdfghjklzxcvbnm     \n\n"

def fill_dir(N, max_size):
    if os.path.exists('./test'):
        shutil.rmtree('./test')
    os.mkdir('./test')
    data = []
    for i in range(N):
        with open('./test/file{}.txt'.format(i), 'w') as f:
            data_size = random.randint(0, max_size)
            data = "".join([random.choice(alphabet) for x in range(data_size)])
            f.write(data)


def cleanup():
    os.remove(report)
    time.sleep(0.2)

def test_args():
    demon = subprocess.Popen([file, '--dir', './test', '--interval', 'abc'],
                             stderr=subprocess.PIPE)
    errmsg = demon.stderr.readline().decode('ascii')
    assert 'invalid interval argument' in errmsg

    demon = subprocess.Popen([file, '-d', './test', '-n', 'abc'],
                             stderr=subprocess.PIPE)
    errmsg = demon.stderr.readline().decode('ascii')
    assert 'invalid interval argument' in errmsg

    demon = subprocess.Popen([file, '--dir', './does_not_exist', '--interval', '10'],
                             stderr=subprocess.PIPE)
    errmsg = demon.stderr.readline().decode('ascii')
    assert 'failed to open directory' in errmsg

    demon = subprocess.Popen([file, '--interval', '10'],
                             stderr=subprocess.PIPE)
    errmsg = demon.stderr.readline().decode('ascii')
    assert 'dir argument is required' in errmsg

    demon = subprocess.Popen([file, '--dir', './does_not_exist'],
                             stderr=subprocess.PIPE)
    errmsg = demon.stderr.readline().decode('ascii')
    assert 'interval argument is required' in errmsg

    # Let's pass invalid interval to check if environment variables really were read
    my_env = os.environ.copy()
    my_env["dir"] = './test'
    my_env["interval"] = 'abc'
    demon = subprocess.Popen([file], env=my_env,
                             stderr=subprocess.PIPE)
    errmsg = demon.stderr.readline().decode('ascii')
    assert 'invalid interval argument' in errmsg

def test_basic():
    N = 20
    fill_dir(N, 1024 * 1024)

    pid = launch_demon('./test', '1')
    assert pid != 0
    time.sleep(2)
    os.kill(pid, signal.SIGTERM)
    time.sleep(0.5)

    assert os.path.exists(report)
    with open(report, 'r') as f:
        data = json.load(f)
        assert len(data) == N
        for i in range(N):
            assert data[i]['status'] == 'OK'
            assert data[i]['etalon_crc32'] == data[i]['result_crc32']
    cleanup()

def test_signal():
    N = 5
    fill_dir(N, 100)

    pid = launch_demon('./test', '3600')
    assert pid != 0

    assert not os.path.exists(report)
    # Check if ignored signals do not trigger integrity check
    ignored_signals = [signal.SIGQUIT, signal.SIGINT, signal.SIGHUP, signal.SIGCONT]
    for s in ignored_signals:
        os.kill(pid, s)
    time.sleep(1)
    assert not os.path.exists(report)

    # Check if SIGUSR1 triggers integrity check
    os.kill(pid, signal.SIGUSR1)
    time.sleep(0.5)
    os.kill(pid, signal.SIGTERM)
    time.sleep(0.5)

    assert os.path.exists(report)
    with open(report, 'r') as f:
        data = json.load(f)
        assert len(data) == N
        for i in range(N):
            assert data[i]['status'] == 'OK'
            assert data[i]['etalon_crc32'] == data[i]['result_crc32']
    cleanup()

def test_stress_signal():
    """
    Spam the demon with non-blocked signal to check if EINTR is handled correctly.
    """
    N = 5
    fill_dir(N, 40 * 1024)
    timeout = 3

    assert not os.path.exists(report)
    pid = launch_demon('./test', '1')
    assert pid != 0
    t_start = time.monotonic()
    while time.monotonic() - t_start < timeout:
        os.kill(pid, signal.SIGUSR1)
    time.sleep(1)
    assert os.path.exists(report)
    os.kill(pid, signal.SIGTERM)
    time.sleep(1)

    with open(report, 'r') as f:
        data = json.load(f)
        assert len(data) == N
        for i in range(N):
            assert data[i]['status'] == 'OK'
            assert data[i]['etalon_crc32'] == data[i]['result_crc32']
    cleanup()

def test_integrity_fail():
    N = 20
    to_modify = [3, 4, 9, 15, 18]
    to_remove = [1, 6, 8, 17]
    add_num = 4
    fill_dir(N, 1024)
    interval = '1'
    test_inotify = False

    if platform == "linux" or platform == "linux2":
        print('The platform is linux - inotify will be tested as well.')
        interval = '3600'
        test_inotify = True

    pid = launch_demon('./test', interval)
    assert pid != 0

    for i in to_remove:
        os.remove('./test/file{}.txt'.format(i))
    for i in to_modify:
        with open('./test/file{}.txt'.format(i), 'a') as f:
            f.write(random.choice(alphabet))
    for i in range(add_num):
        with open('./test/file{}.txt'.format(N + i), 'w') as f:
            f.write(random.choice(alphabet))

    if test_inotify:
        time.sleep(2)
        assert os.path.exists(report)
        os.remove(report)

    os.kill(pid, signal.SIGUSR1)
    time.sleep(1)
    os.kill(pid, signal.SIGTERM)
    time.sleep(1)

    assert os.path.exists(report)
    del_count = 0
    add_count = 0
    fail_count = 0
    with open(report, 'r') as f:
        data = json.load(f)
        assert len(data) == N + add_num
        for i in range(N + add_num):
            if data[i]['status'] == 'FAIL':
                fail_count += 1
                assert data[i]['etalon_crc32'] != data[i]['result_crc32']
            elif data[i]['status'] == 'NEW':
                add_count += 1
            elif data[i]['status'] == 'ABSENT':
                del_count += 1
            else:
                assert data[i]['status'] == 'OK'
                assert data[i]['etalon_crc32'] == data[i]['result_crc32']

    assert fail_count == len(to_modify)
    assert add_count == add_num
    assert del_count == len(to_remove)
    cleanup()

print("Test different arguments")
test_args()
print("Test basic")
test_basic()
print("Test signal")
test_signal()
print("Test stress signal")
test_stress_signal()
print("Test integrity fail")
test_integrity_fail()
