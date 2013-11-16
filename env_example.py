import os

for param in os.environ.keys():
    print "%20s %s" % (param,os.environ[param])
