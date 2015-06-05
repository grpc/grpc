#!/bin/sh

CLIENT_ID='1018396037782-tv81fshn76nemr24uuhuginceb9hni2m.apps.googleusercontent.com'
CLIENT_SECRET='_HGHXg4DAA59r4w4x8p6ARzD'
GRANT_TYPE='http://oauth.net/grant_type/device/1.0'
ACCESS_TOKENS_DIR='/tmp/auth_lead_access_tokens'
AUTH_TOKEN_LINK='https://www.googleapis.com/oauth2/v3/token'
GOOGLE_ACCOUNTS_LINK='https://accounts.google.com/o/oauth2/device/code'
USER_INFO_LINK='https://www.googleapis.com/oauth2/v1/userinfo'
#Performs first time authentication
#Or re-authentication if refresh token expires
RE_AUTHENTICATE() {
  INIT_AUTH_JSON=$(curl -s -d "client_id=$CLIENT_ID&scope=email profile" $GOOGLE_ACCOUNTS_LINK)

  USER_CODE=$(echo $INIT_AUTH_JSON | jq .user_code | sed -e 's/^"//' -e 's/"$//')
  echo 'Please use the following user code in the browser:' $USER_CODE
  echo

  VERIFICATION_URL=$(echo $INIT_AUTH_JSON | jq '.verification_url' | sed -e 's/^"//' -e 's/"$//')
  echo 'Verification URL:' $VERIFICATION_URL
  echo

  xdg-open $VERIFICATION_URL

  DEVICE_CODE=$(echo $INIT_AUTH_JSON | jq '.device_code' | sed -e 's/^"//' -e 's/"$//')
  INTERVAL=$(echo $INIT_AUTH_JSON | jq '.interval' | sed -e 's/^"//' -e 's/"$//')

  AUTH_JSON=$(curl -s -d "client_id=$CLIENT_ID&client_secret=$CLIENT_SECRET&code=$DEVICE_CODE&grant_type=$GRANT_TYPE" $AUTH_TOKEN_LINK)
  ACCESS_TOKEN=$(echo $AUTH_JSON | jq '.access_token' | sed -e 's/^"//' -e 's/"$//')

  while [ $ACCESS_TOKEN == 'null' ]
  do
    sleep $INTERVAL
    AUTH_JSON=$(curl -s -d "client_id=$CLIENT_ID&client_secret=$CLIENT_SECRET&code=$DEVICE_CODE&grant_type=$GRANT_TYPE" $AUTH_TOKEN_LINK)
    ACCESS_TOKEN=$(echo $AUTH_JSON | jq '.access_token' | sed -e 's/^"//' -e 's/"$//')
  done

  USER_DETAILS=$(curl -s $USER_INFO_LINK?access_token=$ACCESS_TOKEN)
  USER_ID=$(echo $USER_DETAILS | jq '.email' | sed -e 's/^"//' -e 's/"$//' | awk -F"@" '{print $1}' | sed -e 's/\.//g' | awk '{print tolower($0)}')
  echo $AUTH_JSON > $ACCESS_TOKENS_DIR/$USER_ID
}

#Use existing access token
USE_ACCESS_TOKEN() {
  ACCESS_TOKEN=$(jq '.access_token' $ACCESS_TOKENS_DIR/$USER_ID | sed -e 's/^"//' -e 's/"$//')

  USER_DETAILS=$(curl -s $USER_INFO_LINK?access_token=$ACCESS_TOKEN)

  ID=$(echo $USER_DETAILS | jq '.id' | sed -e 's/^"//' -e 's/"$//')

  if [ $ID == 'null' ]; then
    REFRESH_ACCESS_TOKEN
  fi
}

#Obtain new access token using refresh token
REFRESH_ACCESS_TOKEN() {
  REFRESH_TOKEN=$(jq '.refresh_token' $ACCESS_TOKENS_DIR/$USER_ID | sed -e 's/^"//' -e 's/"$//')
  if [ $REFRESH_TOKEN == 'null' ]; then
    RE_AUTHENTICATE
  else
    REFRESH_JSON=$(curl -s -d "refresh_token=$REFRESH_TOKEN&client_id=$CLIENT_ID&client_secret=$CLIENT_SECRET&grant_type=refresh_token" $AUTH_TOKEN_LINK)
    
    ACCESS_TOKEN=$(echo $REFRESH_JSON | jq '.access_token')
    if [ $ACCESS_TOKEN == 'null' ]; then
      RE_AUTHENTICATE
    else
      NEW_AUTH_JSON=$(jq ".access_token=$ACCESS_TOKEN" $ACCESS_TOKENS_DIR/$USER_ID)
      echo $NEW_AUTH_JSON > $ACCESS_TOKENS_DIR/$USER_ID
    fi
  fi
}

#create directory to store tokens, if not already present
[ ! -d $ACCESS_TOKENS_DIR ] && mkdir $ACCESS_TOKENS_DIR

#Convert user entered email id to unique string by converting to splitting on '@' symbol, if present,
#removing '.'s and converting to lowercase
USER_ID=$(echo $2 | awk -F"@" '{print $1}' | sed -e 's/\.//g' | awk '{print tolower($0)}')

if [ -s $ACCESS_TOKENS_DIR/$USER_ID ]; then
  USE_ACCESS_TOKEN
else
  RE_AUTHENTICATE
fi

./$1 --access_token=$ACCESS_TOKEN