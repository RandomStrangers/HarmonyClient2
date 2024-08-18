DESKTOP_FILE=HarmonyClient.desktop
GAME_DIR=`pwd`

# remove existing HarmonyClient desktop entry file
rm $DESKTOP_FILE

# download HarmonyClient icon from github if necessary
if [ -f "Harmony_full.png" ]
then
  echo "Harmony_full.png exists already. Skipping download."
else
  echo "Harmony_full.png doesn't exist. Attempting to download it.."
  wget "https://raw.githubusercontent.com/RandomStrangers/HarmonyClient/master/misc/Icon.png"
fi

# create HarmonyClient desktop entry
echo 'Creating HarmonyClient.desktop..'
cat >> $DESKTOP_FILE << EOF
[Desktop Entry]
Type=Application
Comment=Minecraft Classic inspired sandbox game
Name=HarmonyClient
Exec=$GAME_DIR/HarmonyClient
Icon=$GAME_DIR/Harmony_full.png
Path=$GAME_DIR
Terminal=false
Categories=Game
EOF
chmod +x $DESKTOP_FILE

echo 'Installing HarmonyClient.desktop..'
# install HarmonyClient desktop entry into the system
sudo desktop-file-install --dir=/usr/share/applications HarmonyClient.desktop
sudo update-desktop-database /usr/share/applications
