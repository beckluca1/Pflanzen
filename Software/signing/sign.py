import os
import sys
import json
import hashlib
import nacl.signing
import nacl.encoding

# ---------- Load keys from file ----------
def load_keys(path="keys.txt"):
    private_key = None
    public_key = None

    with open(path, "r") as f:
        for line in f:
            line = line.strip()

            if line.startswith("Private_Key:"):
                private_key = line.split("Private_Key:")[1].strip()

            elif line.startswith("Public_Key"):
                public_key = line.split(":")[1].strip()

    if not private_key or not public_key:
        raise ValueError("Keys not found or invalid format in keys.txt")

    return private_key, public_key


def file_sha256(file_path):
    sha = hashlib.sha256()

    with open(file_path, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            sha.update(chunk)

    return sha.digest()  # raw bytes (important for signing)


# ---------- Sign file ----------
def sign_file(file_path, signing_key):
    file_hash = file_sha256(file_path)
    signed = signing_key.sign(file_hash)
    return signed.signature.hex()


# ---------- Write .sig file ----------
def write_sig_file(file_path, signature):
    sig_path = file_path + ".sig"

    with open(sig_path, "w") as f:
        f.write(signature)

    return sig_path


# ---------- Sign folder ----------
def sign_folder(folder_path, private_key_hex):
    signing_key = nacl.signing.SigningKey(
        private_key_hex,
        encoder=nacl.encoding.HexEncoder
    )

    for root, _, files in os.walk(folder_path):
        for file in files:
            file_path = os.path.join(root, file)

            # 🚫 skip .sig files
            if file.endswith(".sig"):
                continue

            try:
                signature = sign_file(file_path, signing_key)
                sig_file = write_sig_file(file_path, signature)

                print(f"Signed: {file_path} -> {sig_file}")

            except Exception as e:
                print(f"Failed: {file_path} ({e})")


# ---------- CLI ----------
if __name__ == "__main__":
    key_path = sys.argv[1]
    private_key, public_key = load_keys(key_path )
    folder_path = sys.argv[2]    
    sig = sign_folder(folder_path, private_key)
    print("Signature:", sig)