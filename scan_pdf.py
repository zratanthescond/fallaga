
import re
import os

pdf_path = r'C:\Users\honco\.gemini\antigravity\brain\fd35a4a2-6c1d-4acf-802d-01f3e3432e1d\.tempmediaStorage\67d2fb7ad82fdd79.pdf'

def scan_pdf(path):
    if not os.path.exists(path):
        print(f"File not found: {path}")
        return

    with open(path, 'rb') as f:
        data = f.read()

    # Look for ASCII-like text segments
    # We search for sequences of 5+ alphanumeric/punctuation chars
    text_segments = re.findall(b'[\x20-\x7E]{5,}', data)
    
    keywords = [b'Cothon', b'basin', b'bassin', b'Hadrum', b'm ', b'metres', b'width', b'length', b'perim', b'wall', b'muraille', b'Kasbah', b'Ribat']
    
    found = []
    for seg in text_segments:
        if any(k.lower() in seg.lower() for k in keywords):
            try:
                found.append(seg.decode('ascii', errors='ignore'))
            except:
                pass
    
    # Also search for numbers near 'm' or 'mètres'
    # Use a broader search for 2-4 digit numbers
    numbers = re.findall(b'\d{2,4}', data)
    
    print("--- TEXT SEGMENTS WITH KEYWORDS ---")
    for s in found[:30]:
        print(s)
        
    print("\n--- FREQUENT NUMBERS ---")
    num_counts = {}
    for n in numbers:
        ns = n.decode()
        num_counts[ns] = num_counts.get(ns, 0) + 1
    
    # Sort and print most common multi-digit numbers
    sorted_nums = sorted(num_counts.items(), key=lambda x: x[1], reverse=True)
    for n, count in sorted_nums[:30]:
        print(f"{n}: {count} occurrences")
    
    print("\n--- TOPOGRAPHY KEYWORDS ---")
    topo_keywords = [b'Plateau', b'Kasbah', b'Hippodrome', b'Cirque', b'Citerne', b'Sofra', b'Bassin', b'Oued']
    
    for k in topo_keywords:
        # Simple byte search
        count = data.lower().count(k.lower())
        if count > 0:
            print(f"Found {k.decode()} : {count} times")
            # Try to find context (30 chars before/after)
            indices = [m.start() for m in re.finditer(re.escape(k), data, re.IGNORECASE)]
            for i in indices[:3]:
                start = max(0, i - 40)
                end = min(len(data), i + 40)
                snippet = data[start:end]
                # Clean non-ascii for display
                cln = b''.join([bytes([c]) if 32 <= c <= 126 else b'.' for c in snippet])
                print(f"   Context: {cln.decode()}")

scan_pdf(pdf_path)
