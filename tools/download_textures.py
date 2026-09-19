import os
import urllib.request
import zipfile
import ssl

def download_and_extract(url, dest_dir):
    print(f"Downloading {url}...")
    zip_path = os.path.join(dest_dir, "temp.zip")
    os.makedirs(dest_dir, exist_ok=True)
    
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    
    # Set standard browser user agent
    req = urllib.request.Request(
        url, 
        headers={'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.3'}
    )
    
    try:
        with urllib.request.urlopen(req, context=ctx) as response, open(zip_path, 'wb') as out_file:
            out_file.write(response.read())
        print("Download complete. Extracting...")
        with zipfile.ZipFile(zip_path, 'r') as zip_ref:
            zip_ref.extractall(dest_dir)
        os.remove(zip_path)
        print(f"Extraction complete in {dest_dir}")
    except Exception as e:
        print(f"Error downloading/extracting {url}: {e}")

if __name__ == "__main__":
    dest_base = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "assets", "terrain"))
    
    # 1. Lush Green Grass (Grass004)
    grass_url = "https://ambientcg.com/get?file=Grass004_2K-JPG.zip"
    grass_dir = os.path.join(dest_base, "grass_texture")
    download_and_extract(grass_url, grass_dir)
    
    # 2. Rock Cliff (Rock030)
    rock_url = "https://ambientcg.com/get?file=Rock030_2K-JPG.zip"
    rock_dir = os.path.join(dest_base, "rocks_texture")
    download_and_extract(rock_url, rock_dir)
