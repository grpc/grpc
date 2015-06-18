#!/usr/bin/python
#
# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

import os
import sys
import re
import urllib2
import urllib
import json
import time
import subprocess
import fnmatch

CLIENT_ID = '1018396037782-tv81fshn76nemr24uuhuginceb9hni2m.apps.googleusercontent.com'
CLIENT_SECRET = '_HGHXg4DAA59r4w4x8p6ARzD'
GRANT_TYPE = 'http://oauth.net/grant_type/device/1.0'
ACCESS_TOKENS_DIR = '/tmp/auth_lead_access_tokens'
AUTH_TOKEN_LINK = 'https://www.googleapis.com/oauth2/v3/token'
GOOGLE_ACCOUNTS_LINK = 'https://accounts.google.com/o/oauth2/device/code'
USER_INFO_LINK = 'https://www.googleapis.com/oauth2/v1/userinfo'

# Fetches JSON reply object, given a url and parameters
def fetchJSON(url, paramDict):
  if len(paramDict) == 0:
    req = urllib2.Request(url)
  else:
    data = urllib.urlencode(paramDict)
    req = urllib2.Request(url, data)

  try:
    response = urllib2.urlopen(req)
    result = response.read()

  except urllib2.HTTPError, error:
    result = error.read()

  return result

# Fetch user info; used to check if access token is valid
def getUserInfo(accessToken):
  url = USER_INFO_LINK + '?access_token=' + accessToken
  paramDict = {}
  JSONBody = fetchJSON(url, paramDict)
  data = json.loads(JSONBody)

  return data

# Returns true if stored access token is valid
def isAccessTokenValid(accessToken):
  data = getUserInfo(accessToken);

  if 'id' in data:
    return True
  else:
    return False

# Returns user id given a working access token
def getUserId(accessToken):
  data = getUserInfo(accessToken)

  email = data['email']
  userId = getUserIdFromEmail(email)

  return userId

# Extracts a unique user id from an email address
def getUserIdFromEmail(email):
  email = email.split('@')[0].lower() # take username and convert to lower case
  userId = re.sub('[.]', '', email) # remove periods

  return userId

# Use an existing access token
def useAccessToken(userTokFile):
  with open(userTokFile, "r") as data_file:
    data = json.load(data_file) # load JSON data from file
    accessToken = data["access_token"]

    # If access token has gone stale, refresh it
    if not isAccessTokenValid(accessToken):
      return refreshAccessToken(data["refresh_token"], userTokFile)

    return accessToken

# refresh stale access token
def refreshAccessToken(refreshToken, userTokFile):
  # Parameters for request
  paramDict = {'refresh_token':refreshToken, 'client_id':CLIENT_ID, 'client_secret':CLIENT_SECRET, 'grant_type':'refresh_token'}
  # Fetch reply to request
  JSONBody = fetchJSON(AUTH_TOKEN_LINK, paramDict)
  data = json.loads(JSONBody)

  if not 'access_token' in data:
    # Refresh token has gone stale, re-authentication required
    return reauthenticate()
  else:
    # write fresh access token to tokens file
    tokenData = {}

    with open(userTokFile, "r") as data_file:
      tokenData = json.load(data_file)
    
    with open(userTokFile, "w") as data_file:
      tokenData['access_token'] = data['access_token']
      json.dump(tokenData, data_file)

    # return fresh access token
    return data['access_token']

def reauthenticate():
  # Request parameters
  paramDict = {'client_id':CLIENT_ID, 'scope':'email profile'}
  JSONBody = fetchJSON(GOOGLE_ACCOUNTS_LINK, paramDict)
  data = json.loads(JSONBody)

  print 'User authorization required\n'
  print 'Please use the following code in you browser: ', data['user_code'] # Code to be entered by user in browser
  print 'Verification URL: ', data['verification_url'] # Authentication link
  print '\nAwaiting user authorization. May take a few more seconds after authorizing...\n'

  authData = {}

  while not 'access_token' in authData:
    # Request parameters
    authDict = {'client_id':CLIENT_ID, 'client_secret':CLIENT_SECRET, 'code':data['device_code'], 'grant_type':GRANT_TYPE}
    JSONBody = fetchJSON(AUTH_TOKEN_LINK, authDict)
    authData = json.loads(JSONBody)
    # If server pinged too quickly, will get slowdown message; need to wait for specified interval
    time.sleep(data['interval'])

  # File to write tokens
  newUserTokFile = ACCESS_TOKENS_DIR + '/' + getUserId(authData['access_token'])

  # Write tokens to file
  with open(newUserTokFile, "w") as data_file:
    json.dump(authData, data_file)

  # return working access token
  return authData['access_token']

# Fetch a working access token given user entered email id; authntication may be required
def getAccessToken(email):
  # Get unique user id from email address
  userId = getUserIdFromEmail(email)

  # Token file
  userTokFile = ACCESS_TOKENS_DIR + '/' + userId

  accessToken = ''

  if os.path.exists(userTokFile):
    # File containing access token exists; unless refresh token has expired, user authentication will not be required
    accessToken = useAccessToken(userTokFile)
  else:
    # User authentication required
    accessToken = reauthenticate()

  return accessToken

# If user has not entered full path to test, recursively searches for given test in parent folders
def findTestPath(test):
  # If user entered full path to test, return it
  if(os.path.isfile(test)):
    return test

  testName = test.split('/')[-1] # Extract just test name
  testPath = ''

  # Search for test
  for root, dirnames, filenames in os.walk('../../../'):
    for fileName in fnmatch.filter(filenames, testName):
      testPath = os.path.join(root, fileName)

  return testPath

def getSysInfo():
  # Fetch system information
  sysInfo = os.popen('lscpu').readlines()

  NICs = os.popen('ifconfig | cut -c1-8 | sed \'/^\s*$/d\' | sort -u').readlines()
  nicAddrs = os.popen('ifconfig | grep -oE "inet addr:([0-9]{1,3}\.){3}[0-9]{1,3}"').readlines()

  nicInfo = []

  for i in range(0, len(NICs)):
    NIC = NICs[i]
    NIC = re.sub(r'[^\w]', '', NIC)

    ethtoolProcess = subprocess.Popen(["ethtool",NIC], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    ethtoolResult = ethtoolProcess.communicate()[0]

    ethtoolResultList = ethtoolResult.split('\n\t')
    for ethtoolString in ethtoolResultList:
      if ethtoolString.startswith('Speed'):
        ethtoolString = ethtoolString.split(':')[1]
        ethtoolString = ethtoolString.replace('Mb/s',' Mbps')
        nicInfo.append(NIC + ' speed: ' + ethtoolString + '\n')
        nicInfo.append(NIC + ' inet address: ' + nicAddrs[i].split(':')[1])

  print 'Obtaining network info....'
  tcp_rr_rate = str(os.popen('netperf -t TCP_RR -v 0').readlines()[1])
  print 'Network info obtained'
  
  nicInfo.append('TCP RR Transmission Rate per sec: ' + tcp_rr_rate + '\n')
  sysInfo = sysInfo + nicInfo

  return sysInfo

def main():
  # If tokens directory does not exist, creates it
  if not os.path.exists(ACCESS_TOKENS_DIR):
    os.makedirs(ACCESS_TOKENS_DIR)

  if len(sys.argv) > 1:
    test = sys.argv[1]
  else:
    test = raw_input('Enter the test path/name: ')

  if len(sys.argv) > 2:
    email = sys.argv[2]
  else:
    email = raw_input('Enter your e-mail id: ')

  try:
    # Fetch working access token
    accessToken = getAccessToken(email)
  except Exception, e:
    print 'Error in authentication'

  try:
    testPath = findTestPath(test) # Get path to test
    testName = testPath.split('/')[-1] # Get test name

    sysInfo = getSysInfo()

    print '\nBeginning test:\n'
    # Run the test
    subprocess.call([testPath, '--report_metrics_db=true', '--access_token='+accessToken, '--test_name='+testName, '--sys_info='+str(sysInfo).strip('[]')])
  except OSError:
    print 'Could not execute the test, please check test name'

if __name__ == "__main__":
  main()