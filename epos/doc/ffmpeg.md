# FFmpeg
<https://engineering.giphy.com/how-to-make-gifs-with-ffmpeg/>

Scale and convert.

```sh
ffmpeg -ss 5 -t 12 -i v.mkv -filter:v 'fps=30,crop=1920:1080:320:0,scale=1280:-1:flags=lanczos' -an -c:v libx265 -crf 16 v.mp4
```
