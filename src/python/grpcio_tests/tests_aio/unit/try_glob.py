import glob
import os

tests = glob.glob("*_test.py")

for test in sorted(tests)[:5]:
    for root, _, files in os.walk(os.getcwd(), followlinks=True):
        if test in files:
            print(os.path.join(root, test))
