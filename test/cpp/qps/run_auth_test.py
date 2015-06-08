#!/usr/bin/python

import os
import sys
import re
import urllib2
import urllib
import json
import time
import subprocess

CLIENT_ID = '1018396037782-tv81fshn76nemr24uuhuginceb9hni2m.apps.googleusercontent.com'
CLIENT_SECRET = '_HGHXg4DAA59r4w4x8p6ARzD'
GRANT_TYPE = 'http://oauth.net/grant_type/device/1.0'
ACCESS_TOKENS_DIR = '/tmp/auth_lead_access_tokens'
AUTH_TOKEN_LINK = 'https://www.googleapis.com/oauth2/v3/token'
GOOGLE_ACCOUNTS_LINK = 'https://accounts.google.com/o/oauth2/device/code'
USER_INFO_LINK = 'https://www.googleapis.com/oauth2/v1/userinfo'

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

def getUserInfo(accessToken):
  url = USER_INFO_LINK + '?access_token=' + accessToken
  paramDict = {}
  JSONBody = fetchJSON(url, paramDict)
  data = json.loads(JSONBody)
  
  return data

def isAccessTokenValid(accessToken):
  data = getUserInfo(accessToken);

  if 'id' in data:
    return True
  else:
    return False

def getUserId(accessToken):
  data = getUserInfo(accessToken)

  email = data['email']
  email = email.split('@')[0].lower()
  userId = re.sub('[.]', '', email)

  return userId

def useAccessToken(userTokFile):
  with open(userTokFile, "r") as data_file:
    data = json.load(data_file)
    accessToken = data["access_token"]

    if not isAccessTokenValid(accessToken):
      return refreshAccessToken(data["refresh_token"], userTokFile)

    return accessToken

def refreshAccessToken(refreshToken, userTokFile):
  paramDict = {'refresh_token':refreshToken, 'client_id':CLIENT_ID, 'client_secret':CLIENT_SECRET, 'grant_type':'refresh_token'}
  JSONBody = fetchJSON(AUTH_TOKEN_LINK, paramDict)
  data = json.loads(JSONBody)
  if not 'access_token' in data:
    return reauthenticate()
  else:
    tokenData = {}

    with open(userTokFile, "r") as data_file:
      tokenData = json.load(data_file)
    
    with open(userTokFile, "w") as data_file:
      tokenData['access_token'] = data['access_token']
      json.dump(tokenData, data_file)
    
    return data['access_token']

def reauthenticate():
  paramDict = {'client_id':CLIENT_ID, 'scope':'email profile'}
  JSONBody = fetchJSON(GOOGLE_ACCOUNTS_LINK, paramDict)
  data = json.loads(JSONBody)

  print 'User authorization required\n'
  print 'Please use the following code in you browser: ', data['user_code']
  print 'Verification URL: ', data['verification_url']
  print '\nAwaiting user authorization. May take a few more seconds after authorizing...\n'

  authData = {}

  while not 'access_token' in authData:
    authDict = {'client_id':CLIENT_ID, 'client_secret':CLIENT_SECRET, 'code':data['device_code'], 'grant_type':GRANT_TYPE}
    JSONBody = fetchJSON(AUTH_TOKEN_LINK, authDict)
    authData = json.loads(JSONBody)
    time.sleep(data['interval'])

  newUserTokFile = ACCESS_TOKENS_DIR + '/' + getUserId(authData['access_token'])

  with open(newUserTokFile, "w") as data_file:
    json.dump(authData, data_file)

  return authData['access_token']

def main():
  if not os.path.exists(ACCESS_TOKENS_DIR):
    os.makedirs(ACCESS_TOKENS_DIR)

  email = sys.argv[2]
  email = email.split('@')[0].lower()
  userId = re.sub('[.]', '', email)

  userTokFile = ACCESS_TOKENS_DIR + '/' + userId

  accessToken = ''

  if os.path.exists(userTokFile):
    accessToken = useAccessToken(userTokFile)
  else:
    accessToken = reauthenticate()

  testName = sys.argv[1].split('/')[-1]
  subprocess.call([sys.argv[1], '--access_token='+accessToken, '--test_name='+testName])

if __name__ == "__main__":
  main()