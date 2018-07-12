//=========================================================
//  MusE
//  Linux Music Editor
//  $Id: wavetrack.cpp,v 1.15.2.12 2009/12/20 05:00:35 terminator356 Exp $
//
//  (C) Copyright 2003 Werner Schweer (ws@seh.de)
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; version 2 of
//  the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//
//=========================================================

#include <math.h>

#include "track.h"
#include "event.h"
#include "audio.h"
#include "wave.h"
#include "xml.h"
#include "song.h"
#include "globals.h"
#include "gconfig.h"
#include "al/dsp.h"
// REMOVE Tim. latency. Added.
#include "latency_compensator.h"

//#define WAVETRACK_DEBUG

namespace MusECore {

//---------------------------------------------------------
//   WaveTrack
//---------------------------------------------------------

WaveTrack::WaveTrack() : AudioTrack(Track::WAVE)
{
  setChannels(1);
}

WaveTrack::WaveTrack(const WaveTrack& wt, int flags) : AudioTrack(wt, flags)
{
  internal_assign(wt, flags | Track::ASSIGN_PROPERTIES);
}

void WaveTrack::internal_assign(const Track& t, int flags)
{
      if(t.type() != WAVE)
        return;
      //const WaveTrack& wt = (const WaveTrack&)t;

      const bool dup = flags & ASSIGN_DUPLICATE_PARTS;
      const bool cpy = flags & ASSIGN_COPY_PARTS;
      const bool cln = flags & ASSIGN_CLONE_PARTS;
      if(dup || cpy || cln)
      {
        const PartList* pl = t.cparts();
        for (ciPart ip = pl->begin(); ip != pl->end(); ++ip) {
              Part* spart = ip->second;
              Part* dpart = 0;
              if(dup)
                dpart = spart->hasClones() ? spart->createNewClone() : spart->duplicate();
              else if(cpy)
                dpart = spart->duplicate();
              else if(cln)
                dpart = spart->createNewClone();
              if(dpart)
              {
                dpart->setTrack(this);
                parts()->add(dpart);
              }
              }
      }

}

void WaveTrack::assign(const Track& t, int flags)
{
      AudioTrack::assign(t, flags);
      internal_assign(t, flags);
}

//---------------------------------------------------------
//   fetchData
//    called from prefetch thread
//---------------------------------------------------------

void WaveTrack::fetchData(unsigned pos, unsigned samples, float** bp, bool doSeek, bool overwrite)
      {
      #ifdef WAVETRACK_DEBUG
      fprintf(stderr, "WaveTrack::fetchData %s samples:%u pos:%u overwrite:%d\n", name().toLatin1().constData(), samples, pos, overwrite);
      #endif

      // reset buffer to zero
      if(overwrite)
        for (int i = 0; i < channels(); ++i)
            memset(bp[i], 0, samples * sizeof(float));

      // Process only if track is not off.
      if(!off())
      {
        bool do_overwrite = overwrite;
        PartList* pl = parts();
        unsigned n = samples;
        for (iPart ip = pl->begin(); ip != pl->end(); ++ip) {
              WavePart* part = (WavePart*)(ip->second);
              if (part->mute())
                  continue;

              unsigned p_spos = part->frame();
              unsigned p_epos = p_spos + part->lenFrame();
              if (pos + n < p_spos)
                break;
              if (pos >= p_epos)
                continue;

              for (iEvent ie = part->nonconst_events().begin(); ie != part->nonconst_events().end(); ++ie) {
                    Event& event = ie->second;
                    unsigned e_spos  = event.frame() + p_spos;
                    unsigned nn      = event.lenFrame();
                    unsigned e_epos  = e_spos + nn;

                    if (pos + n < e_spos)
                      break;
                    if (pos >= e_epos)
                      continue;

                    int offset = e_spos - pos;

                    unsigned srcOffset, dstOffset;
                    if (offset > 0) {
                          nn = n - offset;
                          srcOffset = 0;
                          dstOffset = offset;
                          }
                    else {
                          srcOffset = -offset;
                          dstOffset = 0;

                          nn += offset;
                          if (nn > n)
                                nn = n;
                          }
                    float* bpp[channels()];
                    for (int i = 0; i < channels(); ++i)
                          bpp[i] = bp[i] + dstOffset;

                    event.readAudio(part, srcOffset, bpp, channels(), nn, doSeek, do_overwrite);
                    do_overwrite = false;
                    }
              }
      }

      if(overwrite && MusEGlobal::config.useDenormalBias) {
            // add denormal bias to outdata
            for (int i = 0; i < channels(); ++i)
                  for (unsigned int j = 0; j < samples; ++j)
                      bp[i][j] +=MusEGlobal::denormalBias;
            }

      _prefetchFifo.add();
      }

//---------------------------------------------------------
//   write
//---------------------------------------------------------

void WaveTrack::write(int level, Xml& xml) const
      {
      xml.tag(level++, "wavetrack");
      AudioTrack::writeProperties(level, xml);
      const PartList* pl = cparts();
      for (ciPart p = pl->begin(); p != pl->end(); ++p)
            p->second->write(level, xml);
      xml.etag(level, "wavetrack");
      }

//---------------------------------------------------------
//   read
//---------------------------------------------------------

void WaveTrack::read(Xml& xml)
      {
      for (;;) {
            Xml::Token token = xml.parse();
            const QString& tag = xml.s1();
            switch (token) {
                  case Xml::Error:
                  case Xml::End:
                        goto out_of_WaveTrackRead_forloop;
                  case Xml::TagStart:
                        if (tag == "part") {
                              Part* p = 0;
                              p = Part::readFromXml(xml, this);
                              if(p)
                                parts()->add(p);
                              }
                        else if (AudioTrack::readProperties(xml, tag))
                              xml.unknown("WaveTrack");
                        break;
                  case Xml::Attribut:
                        break;
                  case Xml::TagEnd:
                        if (tag == "wavetrack") {
                              mapRackPluginsToControllers();
                              goto out_of_WaveTrackRead_forloop;
                              }
                  default:
                        break;
                  }
            }
out_of_WaveTrackRead_forloop:
      chainTrackParts(this);
      }

//---------------------------------------------------------
//   newPart
//---------------------------------------------------------

Part* WaveTrack::newPart(Part*p, bool clone)
      {
      WavePart* part = clone ? (WavePart*)p->createNewClone() : (WavePart*)p->duplicate();
      part->setTrack(this);
      return part;
      }

bool WaveTrack::openAllParts()
{
  bool opened = false;
  const PartList* pl = parts();
  for(ciPart ip = pl->begin(); ip != pl->end(); ++ip)
  {
    if(ip->second->openAllEvents())
      opened = true;
  }
  return opened;
}
      
bool WaveTrack::closeAllParts()
{
  bool closed = false;
  const PartList* pl = parts();
  for(ciPart ip = pl->begin(); ip != pl->end(); ++ip)
  {
    if(ip->second->closeAllEvents())
      closed = true;
  }
  return closed;
}

//---------------------------------------------------------
//   getDataPrivate
//    return false if no data available
//---------------------------------------------------------

bool WaveTrack::getDataPrivate(unsigned pos, int channels, unsigned nframes, float** buffer)
      {
// TODO TODO TODO: Adjust compensation for when 'monitor' is on !!! It will change the latency.
        
        
        
        
        
      // use supplied buffers

// REMOVE Tim. latency. Changed.
//       RouteList* rl = inRoutes();
      const RouteList* rl = inRoutes();
      const int rl_sz = rl->size();

      #ifdef NODE_DEBUG_PROCESS
      fprintf(stderr, "AudioTrack::getData name:%s channels:%d inRoutes:%d\n", name().toLatin1().constData(), channels, int(rl->size()));
      #endif

      bool have_data = false;
      bool used_in_chan_array[channels];
      for(int i = 0; i < channels; ++i)
        used_in_chan_array[i] = false;

// REMOVE Tim. latency. Added.
//       struct buf_pos_struct {
//         int _channels;
//         unsigned long _pos;
//         buf_pos_struct(int channels, unsigned long pos) { _channels = channels; _pos = pos; }
//       };
//       int tot_chans = 0;
//       for (ciRoute ir = rl->begin(); ir != rl->end(); ++ir) {
//             if(ir->type != Route::TRACK_ROUTE || !ir->track || ir->track->isMidiTrack())
//               continue;
//             AudioTrack* atrack = static_cast<AudioTrack*>(ir->track);
//           ++tot_chans;
//       }
//       int idx = 0;
      unsigned long latency_array[rl_sz]; // REMOVE Tim. latency. Added.
      int latency_array_cnt = 0;
      unsigned long route_worst_case_latency = 0;
      for (ciRoute ir = rl->begin(); ir != rl->end(); ++ir, ++latency_array_cnt) {
            if(ir->type != Route::TRACK_ROUTE || !ir->track || ir->track->isMidiTrack())
              continue;
            AudioTrack* atrack = static_cast<AudioTrack*>(ir->track);
//             const int atrack_out_channels = atrack->totalOutChannels();
//             const int src_ch = ir->remoteChannel <= -1 ? 0 : ir->remoteChannel;
//             const int src_chs = ir->channels;
//             int fin_src_chs = src_chs;
//             if(src_ch + fin_src_chs >  atrack_out_channels)
//               fin_src_chs = atrack_out_channels - src_ch;
//             const int next_src_chan = src_ch + fin_src_chs;
//             // The goal is to have equal latency output on all channels on this track.
//             for(int i = src_ch; i < next_src_chan; ++i)
//             {
//               const float lat = atrack->trackLatency(i);
//               if(lat > worst_case_latency)
//                 worst_case_latency = lat;
//             }
            //const unsigned long lat = atrack->outputLatency();
            //latency_array[latency_array_cnt] = lat;
            //if(lat > route_worst_case_latency)
            //  route_worst_case_latency = lat;
            const TrackLatencyInfo li = atrack->getLatencyInfo();
            latency_array[latency_array_cnt] = li._outputLatency;
            if(li._outputLatency > route_worst_case_latency)
              route_worst_case_latency = li._outputLatency;
            }
            
      // Adjust for THIS track's contribution to latency.
      // The goal is to have equal latency output on all channels on this track.
      const int track_out_channels = totalOutChannels();
      //const int track_out_channels = totalProcessBuffers();
      unsigned long track_worst_case_chan_latency = 0;
      for(int i = 0; i < track_out_channels; ++i)
      {
        const unsigned long lat = trackLatency(i);
        if(lat > track_worst_case_chan_latency)
          track_worst_case_chan_latency = lat;
      }
      
      //return worst_case_latency;
      //latency_array[0] = latency_array[0]; // REMOVE. Just to compile...
      
      const unsigned long total_latency = route_worst_case_latency + track_worst_case_chan_latency;
      
      
      // Now prepare the latency array to be passed to the compensator's writer,
      //  by adjusting each route latency value. ie. the route with the worst-case
      //  latency will get ZERO delay, while routes having smaller latency will get
      //  MORE delay, to match all the signals' timings together.
      for(int i = 0; i < rl_sz; ++i)
        latency_array[i] = total_latency - latency_array[i];
      

      // REMOVE Tim. latency. Added.
      latency_array_cnt = 0;
      
// REMOVE Tim. latency. Changed.
//       for (ciRoute ir = rl->begin(); ir != rl->end(); ++ir) {
      for (ciRoute ir = rl->begin(); ir != rl->end(); ++ir, ++latency_array_cnt) {
// REMOVE Tim. latency. Changed.
//             if(ir->track->isMidiTrack())
            if(ir->type != Route::TRACK_ROUTE || !ir->track || ir->track->isMidiTrack())
              continue;

            // Only this track knows how many destination channels there are,
            //  while only the route track knows how many source channels there are.
            // So take care of the destination channels here, and let the route track handle the source channels.
            const int dst_ch = ir->channel <= -1 ? 0 : ir->channel;
            if(dst_ch >= channels)
              continue;
            const int dst_chs = ir->channels <= -1 ? channels : ir->channels;
            const int src_ch = ir->remoteChannel <= -1 ? 0 : ir->remoteChannel;
            const int src_chs = ir->channels;

            int fin_dst_chs = dst_chs;
            if(dst_ch + fin_dst_chs > channels)
              fin_dst_chs = channels - dst_ch;

            #ifdef NODE_DEBUG_PROCESS
            fprintf(stderr, "    calling copy/addData on %s dst_ch:%d dst_chs:%d fin_dst_chs:%d src_ch:%d src_chs:%d ...\n",
                    ir->track->name().toLatin1().constData(),
                    dst_ch, dst_chs, fin_dst_chs,
                    src_ch, src_chs);
            #endif

            static_cast<AudioTrack*>(ir->track)->copyData(pos,
                                                          dst_ch, dst_chs, fin_dst_chs,
                                                          src_ch, src_chs,
                                                          nframes, buffer,
// REMOVE Tim. latency. Changed.
//                                                           false, used_in_chan_array);
                                                          false);
            
            // REMOVE Tim. latency. Added.
            // Write the buffers to the latency compensator.
            // By now, each copied channel should have the same latency, 
            //  so we use this convenient common-latency version of write.
            // TODO: Make it per-channel.
            if(_latencyComp)
              _latencyComp->write(nframes, latency_array[latency_array_cnt], buffer);
            
            const int next_chan = dst_ch + fin_dst_chs;
            for(int i = dst_ch; i < next_chan; ++i)
              used_in_chan_array[i] = true;
            have_data = true;
            }

//       // Fill unused channels with silence.
      for(int i = 0; i < channels; ++i)
      {
        if(used_in_chan_array[i])
        {
          // REMOVE Tim. latency. Added.
          // Read back the latency compensated signals, using the buffers in-place.
          if(_latencyComp)
            _latencyComp->read(i, nframes, buffer[i]);
        
          continue;
        }
        // Fill unused channels with silence.
        // Channel is unused. Zero the supplied buffer.
        if(MusEGlobal::config.useDenormalBias)
        {
          for(unsigned int q = 0; q < nframes; ++q)
            buffer[i][q] = MusEGlobal::denormalBias;
        }
        else
          memset(buffer[i], 0, sizeof(float) * nframes);
      }

      return have_data;
      }

//---------------------------------------------------------
//   getData
//---------------------------------------------------------

bool WaveTrack::getData(unsigned framePos, int dstChannels, unsigned nframe, float** bp)
{
  bool have_data = false;
  
  const bool track_rec_flag = recordFlag();
  const bool track_rec_monitor = recMonitor();        // Separate monitor and record functions.

  //---------------------------------------------
  // Contributions to data from input sources:
  //---------------------------------------------

  // Gather input data from connected routes.
  if((MusEGlobal::song->bounceTrack != this) && !noInRoute())
  {
// REMOVE Tim. latency. Changed.
//     have_data = AudioTrack::getData(framePos, dstChannels, nframe, bp);
    // The data retrieved by this will already be latency compensated.
    have_data = getDataPrivate(framePos, dstChannels, nframe, bp);
    
    // Do we want to record the incoming data?
// REMOVE Tim. latency. Changed.
//     if(have_data && track_rec_flag && MusEGlobal::audio->isRecording() && recFile())
    if(have_data && track_rec_flag && 
      (MusEGlobal::audio->isRecording() ||
       (MusEGlobal::song->record() && MusEGlobal::extSyncFlag.value() && MusEGlobal::midiSyncContainer.isPlaying())) && 
      recFile())
    {
      if(MusEGlobal::audio->freewheel())
      {
      }
      else
      {
// REMOVE Tim. latency. Changed.
//         if(fifo.put(dstChannels, nframe, bp, MusEGlobal::audio->pos().frame()))
//           printf("WaveTrack::getData(%d, %d, %d): fifo overrun\n", framePos, dstChannels, nframe);
        
        // REMOVE Tim. latency. Added.
        fprintf(stderr, "WaveTrack::getData: framePos:%d audio pos frame:%d\n", framePos, MusEGlobal::audio->pos().frame());
        
        // This will adjust for the latency before putting.
        putFifo(dstChannels, nframe, bp);
      }
    }
  }

  //---------------------------------------------
  // Contributions to data from playback sources:
  //---------------------------------------------

 if(!MusEGlobal::audio->isPlaying())
  {
    if(!have_data || (track_rec_monitor && have_data))
      return have_data;
    return false;
  }

  // If there is no input source data or we do not want to monitor it,
  //  overwrite the supplied buffers rather than mixing with them.
  const bool do_overwrite = !have_data || !track_rec_monitor;

  // Set the return value.
  have_data = !have_data || (track_rec_monitor && have_data);

  float* pf_buf[dstChannels];
  
  if(MusEGlobal::audio->freewheel())
  {
    // when freewheeling, read data direct from file:
    if(isMute())
    {
      // We are muted. We need to let the fetching progress, but discard the data.
      for(int i = 0; i < dstChannels; ++i)
        // Set to the audio dummy buffer.
        pf_buf[i] = audioOutDummyBuf;
      // Indicate do not seek file before each read.
      fetchData(framePos, nframe, pf_buf, false, do_overwrite);
      // Just return whether we have input sources data.
      return have_data;
    }
    else
    {
      // Not muted. Fetch the data into the given buffers.
      // Indicate do not seek file before each read.
      fetchData(framePos, nframe, bp, false, do_overwrite);
      // We have data.
      return true;
    }
  }
  else
  {
    unsigned pos;
    if(_prefetchFifo.get(dstChannels, nframe, pf_buf, &pos))
    {
      fprintf(stderr, "WaveTrack::getData(%s) (A) fifo underrun\n", name().toLocal8Bit().constData());
      return have_data;
    }
    if(pos != framePos)
    {
      if(MusEGlobal::debugMsg)
        fprintf(stderr, "fifo get error expected %d, got %d\n", framePos, pos);
      while (pos < framePos)
      {
        if(_prefetchFifo.get(dstChannels, nframe, pf_buf, &pos))
        {
          fprintf(stderr, "WaveTrack::getData(%s) (B) fifo underrun\n",
              name().toLocal8Bit().constData());
          return have_data;
        }
      }
    }

    if(isMute())
    {
      // We are muted. We need to let the fetching progress, but discard the data.
      // Just return whether we have input sources data.
      return have_data;
    }

    if(do_overwrite)
    {
      for(int i = 0; i < dstChannels; ++i)
        AL::dsp->cpy(bp[i], pf_buf[i], nframe, MusEGlobal::config.useDenormalBias);
    }
    else
    {
      for(int i = 0; i < dstChannels; ++i)
        AL::dsp->mix(bp[i], pf_buf[i], nframe);
    }
    // We have data.
    return true;
  }

  return have_data;
}

// REMOVE Tim. latency. Added.
//---------------------------------------------------------
//   getLatencyInfo
//---------------------------------------------------------

TrackLatencyInfo WaveTrack::getLatencyInfo()
{
// TODO TODO TODO: Adjust compensation for when 'monitor' is on !!! It will change the latency.
  
  
  
  
  
      // Have we been here before during this process cycle?
      // Just return the cached value.
      if(_latencyInfo._processed)
        return _latencyInfo;
      
      // Get THIS track's channels' worst contribution to latency.
      // The goal is to have equal latency output on all channels on this track.
      const int track_out_channels = totalProcessBuffers(); // totalOutChannels();
      float track_worst_chan_latency = 0.0f;
      for(int i = 0; i < track_out_channels; ++i)
      {
        const float lat = trackLatency(i);
        if(lat > track_worst_chan_latency)
            track_worst_chan_latency = lat;
      }
      
      float route_worst_latency = 0.0f;
//       // These values have a range from 0 (worst) to negative inf (best) or close to it.
      // These values have a range from 0 (worst) to positive inf (best) or close to it.
      // TODO Custom WaveTrack method override for outputLatencyCorrection().
      float track_avail_out_corr = outputLatencyCorrection();
      float route_worst_out_corr = track_avail_out_corr;

      // If record monitor is on we must consider the track's inputs...
      //if(recMonitor())
      {
        const RouteList* rl = inRoutes();
        for (ciRoute ir = rl->begin(); ir != rl->end(); ++ir) {
              if(ir->type != Route::TRACK_ROUTE || !ir->track || ir->track->isMidiTrack())
                continue;
              AudioTrack* atrack = static_cast<AudioTrack*>(ir->track);
  //             const int atrack_out_channels = atrack->totalOutChannels();
  //             const int src_ch = ir->remoteChannel <= -1 ? 0 : ir->remoteChannel;
  //             const int src_chs = ir->channels;
  //             int fin_src_chs = src_chs;
  //             if(src_ch + fin_src_chs >  atrack_out_channels)
  //               fin_src_chs = atrack_out_channels - src_ch;
  //             const int next_src_chan = src_ch + fin_src_chs;
  //             // The goal is to have equal latency output on all channels on this track.
  //             for(int i = src_ch; i < next_src_chan; ++i)
  //             {
  //               const float lat = atrack->trackLatency(i);
  //               if(lat > worst_case_latency)
  //                 worst_case_latency = lat;
  //             }
              TrackLatencyInfo li = atrack->getLatencyInfo();
              if(li._outputLatency > route_worst_latency)
                route_worst_latency = li._outputLatency;
              
  //             if(li._outputAvailableCorrection > route_worst_out_corr)
              if(li._outputAvailableCorrection < route_worst_out_corr)
                route_worst_out_corr = li._outputAvailableCorrection;
              }
              
      }
//       else
//       {
//         float lat = track_worst_chan_latency - track_avail_out_corr;
//         if(lat < 0.0)
//           lat = 0.0;
//         // Reduce the available correction.
//         float new_track_avail_out_corr = track_avail_out_corr - track_worst_chan_latency;
//         if(new_track_avail_out_corr < 0.0)
//           new_track_avail_out_corr = 0.0;
//         track_avail_out_corr = new_track_avail_out_corr;
//         track_worst_chan_latency = lat;
//       }
            
      // The absolute latency of signals leaving this track is the sum of
      //  any connected route latencies and this track's latency.
      _latencyInfo._trackLatency  = track_worst_chan_latency;
      _latencyInfo._outputLatency = track_worst_chan_latency + route_worst_latency;
      _latencyInfo._outputAvailableCorrection = route_worst_out_corr;

      _latencyInfo._processed = true;
      return _latencyInfo;
}

// REMOVE Tim. latency. Added.
//---------------------------------------------------------
//   getForwardLatencyInfo
//---------------------------------------------------------

TrackLatencyInfo WaveTrack::getForwardLatencyInfo()
{
// TODO TODO TODO: Adjust compensation for when 'monitor' is on !!! It will change the latency.
  
  
      // Has the normal reverse latency been processed yet?
      // We need some of the info from the reverse scanning.
      // If all goes well, all nodes should be reverse-processed by now.
      // But in case this one hasn't do it now, starting from this node backwards.
      getLatencyInfo();

      // Have we been here before during this forward scan in this process cycle?
      // Just return the cached value.
      if(_latencyInfo._forwardProcessed)
        return _latencyInfo;
      
      const RouteList* rl = outRoutes();
      float route_worst_latency = 0.0f;
//       // This value has a range from 0 (worst) to negative inf (best) or close to it.
      // This value has a range from 0 (worst) to positive inf (best) or close to it.
      float route_worst_out_corr = 0.0f;
      bool item_found = false;
      for (ciRoute ir = rl->begin(); ir != rl->end(); ++ir) {
            if(ir->type != Route::TRACK_ROUTE || !ir->track || ir->track->isMidiTrack())
              continue;
            AudioTrack* atrack = static_cast<AudioTrack*>(ir->track);
            TrackLatencyInfo li = atrack->getForwardLatencyInfo();
            if(li._forwardOutputLatency > route_worst_latency)
              route_worst_latency = li._forwardOutputLatency;
            
            if(item_found)
            {
//               if(li._outputAvailableCorrection > route_worst_out_corr)
              if(li._outputAvailableCorrection < route_worst_out_corr)
                route_worst_out_corr = li._outputAvailableCorrection;
            }
            else
            {
              route_worst_out_corr = li._outputAvailableCorrection;
              item_found = true;
            }
      }
            
      // Adjust for THIS track's contribution to latency.
      // The goal is to have equal latency output on all channels on this track.
      const int track_out_channels = totalProcessBuffers(); // totalOutChannels();
      float track_worst_chan_latency = 0.0f;
      for(int i = 0; i < track_out_channels; ++i)
      {
        const float lat = trackLatency(i);
        if(lat > track_worst_chan_latency)
            track_worst_chan_latency = lat;
      }
      
      // The absolute latency of signals leaving this track is the sum of
      //  any connected route latencies and this track's latency.
      _latencyInfo._forwardTrackLatency  = track_worst_chan_latency;
      _latencyInfo._forwardOutputLatency = track_worst_chan_latency + route_worst_latency;
      _latencyInfo._forwardOutputAvailableCorrection = route_worst_out_corr;

      _latencyInfo._forwardProcessed = true;
      return _latencyInfo;
}

//---------------------------------------------------------
//   setChannels
//---------------------------------------------------------

void WaveTrack::setChannels(int n)
      {
      AudioTrack::setChannels(n);
      SndFileR sf = recFile();
      if (sf) {
            if (sf->samples() == 0) {
                  sf->remove();
                  sf->setFormat(sf->format(), _channels,
                     sf->samplerate());
                  sf->openWrite();
                  }
            }
      }

} // namespace MusECore
