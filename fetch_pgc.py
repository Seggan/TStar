import requests
import re
import csv
import os

def hms_to_deg(hms_str, is_dec=False):
    s = hms_str.strip()
    if not s: return 0.0
    parts = re.split(r'[:\s]+', s)
    try:
        if len(parts) < 2: return float(parts[0])
        h = float(parts[0])
        m = float(parts[1]) if len(parts) > 1 else 0.0
        sec = float(parts[2]) if len(parts) > 2 else 0.0
        deg = abs(h) + m/60.0 + sec/3600.0
        if h < 0 or s.startswith('-'): deg = -deg
        if not is_dec: deg *= 15.0
        return deg
    except: return 0.0

# Query for galaxies with logD25 >= 1.0 (larger than 1 arcmin)
# This includes all bright galaxies (M31, M33, etc.)
url = "https://vizier.cds.unistra.fr/viz-bin/asu-tsv"
params = {
    "-source": "VII/237/pgc",
    "-out.max": "25000",
    "-out": "PGC,RAJ2000,DEJ2000,logD25,logR25,PA,B_T",
    "logD25": ">=1.0",
    "-sort": "-logD25"
}

print("Download PGC (HyperLeda) largest galaxies...")
r = requests.get(url, params=params)
lines = r.text.split('\n')

os.makedirs('data', exist_ok=True)
count = 0
found_m31 = False

with open('data/pgc.csv', 'w', newline='', encoding='utf-8') as f:
    writer = csv.writer(f)
    writer.writerow(['name', 'ra', 'dec', 'diameter', 'mag', 'alias', 'minorDiameter', 'anglePA'])
    
    in_data = False
    for line in lines:
        if line.startswith('----'):
            in_data = True
            continue
        if not in_data or line.startswith('#') or not line.strip():
            continue
            
        parts = line.split()
        if len(parts) >= 7:
            try:
                # TSV format from earlier check: PGC RA(3) Dec(3) logD25 logR25 PA Mag
                pgc_id = parts[0]
                if not pgc_id[0].isdigit(): continue
                
                ra = hms_to_deg(" ".join(parts[1:4]))
                dec = hms_to_deg(" ".join(parts[4:7]), True)
                
                # Fixed indices based on 3-part RA/Dec
                logd25 = parts[7]
                logr25 = parts[8]
                pa_val = parts[9] if len(parts) > 9 and not parts[9].startswith('(') else "0.0"
                mag_val = parts[10] if len(parts) > 10 else "15.0"
                
                major = (10 ** float(logd25)) * 0.1
                minor = major / (10 ** float(logr25))
                try: pa = float(pa_val)
                except: pa = 0.0
                mag = float(mag_val) if (mag_val and mag_val != "") else 15.0
                
                if pgc_id == "2557": found_m31 = True
                
                writer.writerow([f"PGC {pgc_id}", f"{ra:.6f}", f"{dec:.6f}", f"{major:.3f}", f"{mag:.2f}", "", f"{minor:.3f}", f"{pa:.1f}"])
                count += 1
            except: continue

if count > 0:
    print(f"Successo! Salvate {count} galassie in data/pgc.csv.")
    if found_m31:
        print("M31 (Andromeda) e' presente.")
    else:
        print("ATTENZIONE: M31 non trovata. Controllo i dati...")
else:
    print("ERRORE: Nessun record trovato.")
