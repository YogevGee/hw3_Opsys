import socket
import subprocess
import time
import sys
import os
import signal

# --- CONFIGURATION ---
SERVER_PORT = 9555
HOST = "127.0.0.1"
SERVER_BIN = "./hw3server"

def print_pass(msg):
    print(f"\033[92m[PASS]\033[0m {msg}")

def print_fail(msg, details=""):
    print(f"\033[91m[FAIL]\033[0m {msg}")
    if details: print(f"       Details: {details}")
    sys.exit(1)

def compile_code():
    print("--- 1. Compilation ---")
    if os.path.exists("hw3server"): os.remove("hw3server")
    if os.path.exists("hw3client"): os.remove("hw3client")
    
    ret = subprocess.run(["make"], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if ret.returncode != 0:
        print_fail("Make failed", ret.stderr.decode())
    
    if os.path.exists("hw3server") and os.path.exists("hw3client"):
        print_pass("Code compiled successfully")
    else:
        print_fail("Executables not found after make")

def run_test():
    compile_code()
    
    print("\n--- 2. Server Logic Tests ---")
    # Start Server
    server = subprocess.Popen([SERVER_BIN, str(SERVER_PORT)], 
                              stdout=subprocess.PIPE, 
                              stderr=subprocess.PIPE, 
                              text=True)
    time.sleep(1) # Give it a moment to bind

    try:
        # --- CONNECT CLIENTS (Using raw sockets to simulate strict behavior) ---
        alice = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        alice.connect((HOST, SERVER_PORT))
        alice.sendall(b"Alice\n") # Handshake
        time.sleep(0.1)

        bob = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        bob.connect((HOST, SERVER_PORT))
        bob.sendall(b"Bob\n")
        time.sleep(0.1)

        charlie = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        charlie.connect((HOST, SERVER_PORT))
        charlie.sendall(b"Charlie\n")
        time.sleep(0.1)

        # Clear initial buffers (connection messages)
        alice.setblocking(0)
        bob.setblocking(0)
        charlie.setblocking(0)
        time.sleep(0.5)
        try: alice.recv(4096)
        except: pass
        try: bob.recv(4096)
        except: pass
        try: charlie.recv(4096)
        except: pass

        # --- TEST 1: BROADCAST ---
        alice.sendall(b"Hello Team\n")
        time.sleep(0.2)
        
        msg_b = bob.recv(1024).decode()
        msg_c = charlie.recv(1024).decode()

        if "Alice: Hello Team" in msg_b and "Alice: Hello Team" in msg_c:
            print_pass("Broadcast Message received by all")
        else:
            print_fail("Broadcast failed", f"Bob got: {msg_b}, Charlie got: {msg_c}")

        # --- TEST 2: WHISPER (The tricky part) ---
        # Alice whispers to Bob. Charlie should NOT see it.
        alice.sendall(b"@Bob Secret\n")
        time.sleep(0.2)

        try:
            msg_b = bob.recv(1024).decode()
            if "Alice: @Bob Secret" in msg_b:
                print_pass("Whisper received by target (Bob)")
            else:
                print_fail("Whisper target didn't receive message", f"Got: {msg_b}")
        except BlockingIOError:
            print_fail("Whisper target received NOTHING")

        try:
            msg_c = charlie.recv(1024).decode()
            print_fail("Whisper LEAKED to third party!", f"Charlie saw: {msg_c}")
        except BlockingIOError:
            print_pass("Whisper correctly hidden from third party (Charlie)")

        # --- TEST 3: FUSED PACKETS (TCP Stream robustness) ---
        # Sending two lines in ONE packet. Server must buffer and split them.
        alice.sendall(b"Line1\nLine2\n")
        time.sleep(0.2)
        
        msg_b = bob.recv(4096).decode()
        if "Alice: Line1" in msg_b and "Alice: Line2" in msg_b:
            print_pass("TCP Fused Packets handled correctly (Robustness check)")
        else:
            print_fail("TCP Data Loss detected", f"Server dropped second line. Got: {msg_b}")

        # --- TEST 4: !EXIT HANDLING ---
        # Alice sends !exit. Bob should see message. Alice should disconnect.
        alice.sendall(b"!exit\n")
        time.sleep(0.2)

        msg_b = bob.recv(1024).decode()
        if "Alice: !exit" in msg_b:
            print_pass("Exit notification broadcasted")
        else:
            print_fail("Exit notification not received", f"Got: {msg_b}")
            
        # Verify Alice socket is closed by server (optional, but good)
        try:
            alice.sendall(b"Are you there?")
            # If we get here, socket might still be open, but let's check response
            time.sleep(0.1)
            # If recv returns empty bytes, it's closed
            d = alice.recv(1024)
            if d == b"":
                print_pass("Client socket closed cleanly")
        except:
            print_pass("Client socket closed cleanly")

    except Exception as e:
        print_fail("Test Exception", str(e))
    finally:
        # Cleanup
        server.terminate()
        alice.close()
        bob.close()
        charlie.close()

if __name__ == "__main__":
    run_test()