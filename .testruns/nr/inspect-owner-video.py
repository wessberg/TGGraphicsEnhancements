from pathlib import Path
import cv2
import json
import numpy as np

source = Path(r'C:\Users\Frede\Videos\NVIDIA\Tainted Grail  The Fall of Avalon\Tainted Grail  The Fall of Avalon 2026.10.08 - 15.13.00.01.mp4')
out = Path('artifacts/harness/nr-owner-video-20261008-151300')
out.mkdir(parents=True, exist_ok=True)
cv2.setNumThreads(2)
capture = cv2.VideoCapture(str(source))
fps = capture.get(cv2.CAP_PROP_FPS)
count = int(capture.get(cv2.CAP_PROP_FRAME_COUNT))
metadata = dict(source=str(source), fps=fps, frames=count, duration=count/fps,
                width=int(capture.get(cv2.CAP_PROP_FRAME_WIDTH)), height=int(capture.get(cv2.CAP_PROP_FRAME_HEIGHT)))
(out/'metadata.json').write_text(json.dumps(metadata, indent=2))
targets = {round(t*fps): t for t in np.arange(0, count/fps, .5)}
tiles=[]
for index in range(count):
    ok, frame = capture.read()
    if not ok: break
    if index not in targets: continue
    time=targets[index]
    cv2.imwrite(str(out/f'frame-{time:05.2f}.jpg'), frame, [cv2.IMWRITE_JPEG_QUALITY,95])
    tile=cv2.resize(frame,(640,360),interpolation=cv2.INTER_AREA)
    tile=cv2.copyMakeBorder(tile,28,0,0,0,cv2.BORDER_CONSTANT)
    cv2.putText(tile,f'{time:.2f} s / frame {index}',(10,21),cv2.FONT_HERSHEY_SIMPLEX,.6,(255,255,255),1,cv2.LINE_AA)
    tiles.append(tile)
capture.release()
for page in range((len(tiles)+8)//9):
    group=tiles[page*9:page*9+9]
    group += [np.zeros_like(tiles[0])]*(9-len(group))
    atlas=np.vstack([np.hstack(group[row*3:row*3+3]) for row in range(3)])
    cv2.imwrite(str(out/f'atlas-{page+1}.jpg'),atlas,[cv2.IMWRITE_JPEG_QUALITY,94])
print(json.dumps(metadata))
print(out)
