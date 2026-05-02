from Crypto.Cipher import AES
import struct

SECRET_KEY = "0102030405060708090a0b0c0d0e0f10"

def encrypt_lora_packet(node_id, sequence, command, secret_key_hex):
    # 1. Convert the hex key to bytes (16 bytes / 128 bits)
    key = bytes.fromhex(secret_key_hex)
    
    # 2. Generate a random 2-byte Nonce (short IV)
    # In production, you might use a counter or OS random
    import os
    nonce_short = os.urandom(2)
    
    # 3. Prepare the full 12-byte IV for AES-GCM (padding with zeros)
    # This MUST match the fullIv[12] = {0} logic of the ESP32
    full_iv = nonce_short + b'\x00' * 10
    
    # 4. Prepare the Plaintext: [Sequence (2 bytes)][Command (2 bytes)]
    # 'H' is for unsigned short (16-bit) in Big-Endian (>)
    plaintext = struct.pack('>HH', sequence, command)
    
    # 5. Initialize AES-GCM
    cipher = AES.new(key, AES.MODE_GCM, nonce=full_iv)
    
    # 6. Encrypt and generate the Auth Tag
    ciphertext, auth_tag = cipher.encrypt_and_digest(plaintext)
    
    # 7. Truncate the Auth Tag to 4 bytes (to match ESP32 code)
    truncated_tag = auth_tag[:4]
    
    # 8. Construct the final 11-byte packet
    # [NodeID(1)][Nonce(2)][CipherText(4)][AuthTag(4)]
    packet = bytes([node_id]) + nonce_short + ciphertext + truncated_tag
    
    return packet

# # --- Example Usage ---
# SECRET_KEY = "0102030405060708090a0b0c0d0e0f10" # Must match ESP32
# NODE_ID = 1
# SEQ_NO = 16
# CMD_OPEN = 0x00FF

# encrypted_payload = encrypt_lora_packet(NODE_ID, SEQ_NO, CMD_OPEN, SECRET_KEY)

# print(f"Packet to send (Hex): {encrypted_payload.hex().upper()}")
# print(f"Packet Length: {len(encrypted_payload)} bytes")