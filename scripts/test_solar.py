import json
import time
import os

# Path to telemetry file relative to root directory
DATA_PATH = os.path.join("data", "EVA.json")

def simulate_dial_turn():
    target_angle = 180.0
    print(f"--- Starting Solar Dial Simulation (Target Sun Angle: {target_angle}°) ---")

    # Sweep physical dial from 0 to 360 degrees in 15-degree steps
    for current_angle in range(0, 361, 15):
        if not os.path.exists(DATA_PATH):
            print(f"Error: {DATA_PATH} not found.")
            return

        # 1. Read current JSON
        with open(DATA_PATH, "r") as f:
            data = json.load(f)

        # 2. Update panel position
        if "solar_array" not in data:
            data["solar_array"] = {}

        data["solar_array"]["sun_target_azimuth"] = target_angle
        data["solar_array"]["panel_current_azimuth"] = float(current_angle)

        # 3. Write updated state back to disk
        with open(DATA_PATH, "w") as f:
            json.dump(data, f, indent=2)

        print(f"[SIMULATOR] Dial set to: {current_angle}° | Target: {target_angle}°")
        time.sleep(0.5)

if __name__ == "__main__":
    simulate_dial_turn()
