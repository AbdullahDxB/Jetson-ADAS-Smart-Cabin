sudo jetson_clocks --store
echo "Setting Jetson to MAX PERFORMANCE mode (Fan 100%, Clocks Maxed)"
sudo jetson_clocks

clear
echo "========================================"
echo "    INITIALIZING JETSON HARDWARE        "
echo "========================================"

echo "[1/5] Cleaning up old processes..."
pkill mpv 2>/dev/null
pkill -f jetson_vf.py 2>/dev/null

echo "[2/5] Launching Video Player..."
mpv test.mp4 --idle --geometry=800x600 --loop > /dev/null 2>&1 &

echo "[3/5] Activating AI Environment..."
source ~/my_env/bin/activate
sleep 2

echo "[4/5] Starting HCI Pipeline..."
python3 jetson_vf.py

echo "[5/5] Plotting Results..."
python3 plot_results.py

echo " System Offline. "
echo "Restoring Jetson standard clocks..."
sudo jetson_clocks --restore
echo "========================================"