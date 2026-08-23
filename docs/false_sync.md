# False sync (claude)

Why false syncs happen at all
MP3FindSyncWord (mp3dec.c:110-121) looks for eleven set bits — buf[i] == 0xFF and the top three bits of buf[i+1] set:


if ( (buf[i+0] & SYNCWORDH) == SYNCWORDH && (buf[i+1] & SYNCWORDL) == SYNCWORDL )
Eleven bits of pattern means roughly one chance in 2048 per byte position. Huffman-coded MP3 payload is close enough to random that a 1536-byte buffer has better-than-even odds of containing at least one false hit. So any time you scan raw payload rather than sitting on a genuine frame boundary, you will find syncs that aren't syncs.

That only matters when you've lost sync. In normal operation each successful decode leaves read_ptr exactly on the next real frame, so MP3FindSyncWord returns 0 and never scans payload at all.

The three-line window

301  fhBytes = UnpackFrameHeader(mp3DecInfo, *inbuf);
302  if (fhBytes < 0)
303      return ERR_MP3_INVALID_FRAMEHEADER;
304  *inbuf += fhBytes;
The return at 303 happens before line 304 touches *inbuf, and well before line 316 touches *bytesLeft. So on this specific error the decoder consumed nothing: your read_ptr and bytes_left come back exactly as you passed them in.

That's unusual — it's not true of the neighbouring error paths. ERR_MP3_INVALID_SIDEINFO at 313 returns after *inbuf += fhBytes at 304 but before *bytesLeft -= ... at 316, so it hands you back a pointer and a length that disagree with each other. That inconsistency is exactly why the patch carries frame_start/frame_bytes_left — the pair saved before the call — instead of trusting whatever MP3Decode left behind.

What the old code did with that

backup = read_ptr;  backup_len = bytes_left;
err = MP3Decode(decoder, &read_ptr, &bytes_left, pcm_out, 0);
if (err != 0) break;
...
if (err != 0) { leftover_len = backup_len; memcpy(leftover, backup, leftover_len); }
A false sync at, say, byte 100 of a 1536-byte buffer meant backup_len was 1436 — so it copied 1436 bytes into leftover[512], the unbounded memcpy.

But the livelock was arguably worse. Nothing was consumed, so the next round rebuilt data as leftover (starting with those same false-sync bytes) plus the newly arrived chunk. read_ptr = data, MP3FindSyncWord returned 0 — the same bad byte, now at position 0 — MP3Decode failed identically, and the whole buffer got carried forward again. The decoder never advanced past that one byte. It would sit there consuming the network stream and emitting nothing until something else broke.

What the patch does

if (err == ERR_MP3_INVALID_FRAMEHEADER)
{
    read_ptr = frame_start + 1;
    bytes_left = (int)frame_bytes_left - 1;
    continue;
}
Since nothing was consumed, read_ptr is still sitting on the false sync. Stepping forward exactly one byte means the next MP3FindSyncWord begins after that 0xFF, so it cannot return the same position twice. And continue rescans the rest of the current buffer rather than discarding it.

One byte specifically, because MP3 frames are byte-aligned but nothing stronger — a genuine header can legitimately start at the very next byte, so skipping further risks stepping over the real frame you're trying to resynchronise onto.

This also gives the inner loop a clean termination argument it didn't have before. Every iteration now either decodes a frame (consuming at least the header and side info), skips at least one byte, or breaks. bytes_left strictly decreases, so the loop always makes progress and always ends.

Where it sits in the fix
In the validation run this path reported false syncs: 0. That's expected and it's the point: carrying the unsynced tail keeps the stream in sync, so the decoder never scans raw payload and never manufactures a false sync in the first place. This branch is the safety net for when sync genuinely is lost — a dropped chunk, a truncated stream, a mid-file start, or the moment your server switches to a file with a different format. Before the patch, losing sync once was enough to corrupt the heap. Now it costs a byte.
