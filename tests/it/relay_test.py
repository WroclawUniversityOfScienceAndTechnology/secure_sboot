import subprocess
import time

BOARD = "HURTM"

def set_relay(number: int, enabled: bool) -> None:
    value = 1 if enabled else 0

    subprocess.run(
        ["sudo", "usbrelay", f"{BOARD}_{number}={value}"],
        check=True,
        stdout=subprocess.DEVNULL,
    )

def read_relays() -> str:
    result = subprocess.run(
        ["sudo", "usbrelay"],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


set_relay(2, True)
print(read_relays())       # HURTM_1=1

time.sleep(2)

set_relay(2, False)
print(read_relays())       # HURTM_1=0