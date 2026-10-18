# Downloads the CC0 recordings the game's sounds are built from (Freesound HQ previews) into SourceArt/Audio/Sources.
# Every source page is checked for the CC0 licence before it is used; LICENSES.txt lists them all.
# Usage: python fetch_audio.py
import os, re, sys, json, html, urllib.request

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "SourceArt", "Audio", "Sources"))
UA = {"User-Agent": "Mozilla/5.0"}

# (freesound id, user, what it is used for)
SOURCES = [
    (752630, "modusmogulus", "frag explosion: blast and shrapnel"),
    (734099, "modusmogulus", "frag explosion: detonation crack and reverb"),
    (734100, "modusmogulus", "frag explosion: body layer"),
    (784165, "modusmogulus", "distant explosion tail"),
    (132929, "deleted_user_1887925", "explosion debris"),
    (693421, "areniporgen", "flashbang (CTS 7290)"),
    (737326, "modusmogulus", "flashbang layer"),
    (457294, "brunoboselli", "smoke grenade hiss"),
    (551429, "Themiwa100", "smoke grenade gas"),
    (316682, "Alex_hears_things", "thermite burn (sparkler)"),
    (725813, "F3ather", "grenade pin"),
    (259553, "nicktermer", "grenade spoon"),
    (276600, "insanity54", "kill confirm: armour thud"),
    (237982, "Paul368", "kill confirm: body punch"),
    (259542, "nicktermer", "kill confirm: crunch"),
    (319103, "AbuNijmeh", "breathing after a sprint"),
    (240867, "Shinplaster", "heavy breathing, medium breaths"),
    (395563, "SoundsForHim", "panting: exhausted"),
    (146769, "elle-trudgett", "full inhale and long exhale: breath released after holding"),
    (169931, "u3190", "gasp inhale: breath held"),
    (167034, "soundrecorder7", "light rain"),
    (525046, "speakwithanimals", "rain loop"),
    (446753, "BlueDelta", "heavy thunder strike"),
    (194364, "Dave%20Welsh", "thunder clap"),
    (244053, "lennyboy", "distant thunder"),
    (426894, "danner", "rooftop city ambience"),
    (728589, "Felipe-Vareschi", "rooftop ambience at sunset with wind"),
    (427841, "leonelmail", "quiet city night air tone"),
    (438885, "craigsmith", "wind storm gusts"),
    (574566, "TRP", "blustery wind gusts"),
    (474832, "richwise", "server room tone"),
]


def get(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60).read()


def main():
    os.makedirs(ROOT, exist_ok=True)
    meta = {}
    for sid, user, use in SOURCES:
        page = get(f"https://freesound.org/people/{user}/sounds/{sid}/").decode("utf-8", "replace")
        if "publicdomain/zero" not in page:
            print(f"{sid}: not CC0, skipped")
            continue
        title = re.search(r"<title>(.*?)</title>", page, re.S)
        preview = re.search(r"https://cdn\.freesound\.org/previews/\d+/\d+_\d+-(?:hq|lq)\.(?:mp3|ogg)", page)
        if not preview:
            print(f"{sid}: no preview")
            continue
        base = re.sub(r"-(hq|lq)\.(mp3|ogg)$", "", preview.group(0))
        path = os.path.join(ROOT, f"{sid}.ogg")
        if not os.path.exists(path):
            data = None
            for url, ext in ((base + "-hq.ogg", "ogg"), (base + "-hq.mp3", "mp3")):
                try:
                    data = get(url)
                    path = os.path.join(ROOT, f"{sid}.{ext}")
                    break
                except Exception:  # noqa: BLE001
                    continue
            if not data:
                print(f"{sid}: download failed")
                continue
            with open(path, "wb") as f:
                f.write(data)
        meta[str(sid)] = {"user": user, "title": html.unescape(title.group(1).strip()) if title else "", "use": use,
                          "url": f"https://freesound.org/people/{user}/sounds/{sid}/", "license": "CC0 1.0"}
        print(f"{sid}: ok ({os.path.getsize(path) // 1024} KB) {use}")
    with open(os.path.join(ROOT, "sources.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=1, ensure_ascii=False)
    with open(os.path.join(ROOT, "LICENSES.txt"), "w", encoding="utf-8") as f:
        f.write("Recordings used to build the game's sound effects. All are CC0 1.0 (public domain) on freesound.org.\n\n")
        for sid, m in meta.items():
            f.write(f"{sid}  {m['title']}  by {m['user']}  {m['url']}  ({m['use']})\n")


if __name__ == "__main__":
    sys.exit(main())
