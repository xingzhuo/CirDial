from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parents[1]
asset=root/'packaging/Assets'
asset.mkdir(exist_ok=True)
with Image.open(root/'source/App/VirtualDial/VirtualDial.ico') as im:
    source=im.convert('RGBA')
    for name,size in [('StoreLogo',50),('Square150x150Logo',150),('Square44x44Logo',44)]:
        source.resize((size,size),Image.Resampling.LANCZOS).save(asset/f'{name}.png')
