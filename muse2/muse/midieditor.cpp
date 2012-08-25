//=========================================================
//  MusE
//  Linux Music Editor
//    $Id: midieditor.cpp,v 1.2.2.2 2009/02/02 21:38:00 terminator356 Exp $
//  (C) Copyright 1999 Werner Schweer (ws@seh.de)
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

#include "pos.h"
#include "midieditor.h"
#include "midiedit/ecanvas.h"
#include "waveedit/waveview.h"
#include "scrollscale.h"
#include "mtscale.h"
#include "xml.h"
#include "part.h"
#include "track.h"
#include "song.h"

#include <QRect>
#include <QColor>
#include <QGridLayout>

namespace MusEGui {

//---------------------------------------------------------
//   MidiEditor
//---------------------------------------------------------

MidiEditor::MidiEditor(ToplevelType t, int r, MusECore::PartList* pl,
   QWidget* parent, const char* name) : TopWin(t, parent, name)
      {
      setAttribute(Qt::WA_DeleteOnClose);
      _pl = pl;
      if (_pl)
            for (MusECore::iPart i = _pl->begin(); i != _pl->end(); ++i)
                  _parts.insert(i->second->sn());
      _raster  = r;
      canvas   = 0;
      //wview    = 0;
      _curDrumInstrument = -1;
      mainw    = new QWidget(this);
      
      ///mainGrid = new QGridLayout(mainw);
      mainGrid = new QGridLayout();
      mainw->setLayout(mainGrid);
      
      mainGrid->setContentsMargins(0, 0, 0, 0);
      mainGrid->setSpacing(0);  
      setCentralWidget(mainw);
      
      connect(MusEGlobal::song, SIGNAL(newPartsCreated(const std::map< MusECore::Part*, std::set<MusECore::Part*> >&)), SLOT(addNewParts(const std::map< MusECore::Part*, std::set<MusECore::Part*> >&)));	
      }

//---------------------------------------------------------
//   genPartlist
//---------------------------------------------------------

void MidiEditor::genPartlist()
      {
      _pl->clear();
      for (std::set<int>::iterator i = _parts.begin(); i != _parts.end(); ++i) {
            MusECore::TrackList* tl = MusEGlobal::song->tracks();
            for (MusECore::iTrack it = tl->begin(); it != tl->end(); ++it) {
                  MusECore::PartList* pl = (*it)->parts();
                  MusECore::iPart ip;
                  for (ip = pl->begin(); ip != pl->end(); ++ip) {
                        if (ip->second->sn() == *i) {
                              _pl->add(ip->second);
                              break;
                              }
                        }
                  if (ip != pl->end())
                        break;
                  }
            }
      }

//---------------------------------------------------------
//   addPart
//---------------------------------------------------------

void MidiEditor::addPart(MusECore::Part* p)
{
  _pl->add(p);
  _parts.insert(p->sn());
}


//---------------------------------------------------------
//   MidiEditor
//---------------------------------------------------------

MidiEditor::~MidiEditor()
      {
      if (_pl)
            delete _pl;
      }


//---------------------------------------------------------
//   readStatus
//---------------------------------------------------------

void MidiEditor::readStatus(MusECore::Xml& xml)
      {
      if (_pl == 0)
            _pl = new MusECore::PartList;

      for (;;) {
            MusECore::Xml::Token token = xml.parse();
            QString tag = xml.s1();
            switch (token) {
                  case MusECore::Xml::Error:
                  case MusECore::Xml::End:
                        return;
                  case MusECore::Xml::TagStart:
                        if (tag == "raster")
                              _raster = xml.parseInt();
                        else if (tag == "topwin")
                              TopWin::readStatus(xml);
                        else
                              xml.unknown("MidiEditor");
                        break;
                  case MusECore::Xml::TagEnd:
                        if (tag == "midieditor")
                              return;
                  default:
                        break;
                  }
            }
      }

//---------------------------------------------------------
//   writePartList
//---------------------------------------------------------

void MidiEditor::writePartList(int level, MusECore::Xml& xml) const
      {
      for (MusECore::ciPart p = _pl->begin(); p != _pl->end(); ++p) {
            MusECore::Part* part   = p->second;
            MusECore::Track* track = part->track();
            int trkIdx   = MusEGlobal::song->tracks()->index(track);
            int partIdx  = track->parts()->index(part);
            
            if((trkIdx == -1) || (partIdx == -1))
              printf("MidiEditor::writePartList error: trkIdx:%d partIdx:%d\n", trkIdx, partIdx);
              
            xml.put(level, "<part>%d:%d</part>", trkIdx, partIdx);
            }
      }

//---------------------------------------------------------
//   writeStatus
//---------------------------------------------------------

void MidiEditor::writeStatus(int level, MusECore::Xml& xml) const
      {
      xml.tag(level++, "midieditor");
      TopWin::writeStatus(level, xml);
      xml.intTag(level, "raster", _raster);
      xml.tag(level, "/midieditor");
      }

//---------------------------------------------------------
//   songChanged
//---------------------------------------------------------

void MidiEditor::songChanged(MusECore::SongChangedFlags_t type)
      {
      
      if (type) {
            if (type & (SC_PART_REMOVED | SC_PART_MODIFIED
               | SC_PART_INSERTED | SC_TRACK_REMOVED)) {
                  genPartlist();
                  // close window if editor has no parts anymore
                  if (parts()->empty()) {
                        close();
                        return;
                        }
                  }
            if (canvas)
                  canvas->songChanged(type);
            //else if (wview)
            //      wview->songChanged(type);

            if (type & (SC_PART_REMOVED | SC_PART_MODIFIED
               | SC_PART_INSERTED | SC_TRACK_REMOVED)) {
                  
                  updateHScrollRange();
                  
                  if (canvas)
                        setWindowTitle(canvas->getCaption());
                  //else if (wview)
                  //      setWindowTitle(wview->getCaption());
                  if (type & SC_SIG)
                        time->update();
                        
              }        
            }
      }

//---------------------------------------------------------
//   setCurDrumInstrument
//---------------------------------------------------------

void MidiEditor::setCurDrumInstrument(int instr)
      {
      _curDrumInstrument = instr;
      emit curDrumInstrumentChanged(_curDrumInstrument);
      }

//---------------------------------------------------------
//   curCanvasPart
//---------------------------------------------------------

MusECore::Part* MidiEditor::curCanvasPart() 
{ 
  if(canvas) 
    return canvas->part(); 
  else 
    return 0; 
}

//---------------------------------------------------------
//   setCurCanvasPart
//---------------------------------------------------------

void MidiEditor::setCurCanvasPart(MusECore::Part* part) 
{ 
  if(canvas) 
    canvas->setCurrentPart(part); 
}

// REMOVE Tim.
// MusECore::Pos MidiEditor::rasterStep(MusECore::Pos pos) const 
// { 
//   MusECore::Pos::TType tt = canvas->timeType();
//   pos.setType(tt);
//   switch(tt)
//   {
//     case MusECore::Pos::FRAMES:
// // REMOVE Tim.      
// //       {
// //         unsigned f = pos.frame();
// //         if(_raster == 0)
// //           return f;
// //         unsigned r = f % _raster;
// //         if(r >= _raster / 2)
// //           return f - r + _raster;
// //         else
// //           return f - r;
// //       }
//       return MusECore::Pos(_raster, tt);
//       break;
//     case MusECore::Pos::TICKS:
//       return MusECore::Pos(AL::sigmap.rasterStep(pos.tick(), _raster), tt); 
//       break;
//   }  
//   
//   return pos;
// }
// 
// MusECore::Pos MidiEditor::rasterVal(MusECore::Pos pos) const 
// { 
//   MusECore::Pos::TType tt = canvas->timeType();
//   pos.setType(tt);
//   switch(tt)
//   {
//     case MusECore::Pos::FRAMES:
//       {
//         if(_raster == 0 || _raster == 1)
//           return pos;
//         // TODO: Probably incorrect. Fix proper snapping like sigmap.  // REMOVE Tim.
//         unsigned f = pos.frame();
//         unsigned r = f % _raster;
//         if(r >= _raster / 2)
//           return MusECore::Pos(f - r + _raster, tt);
//         else
//           return MusECore::Pos(f - r, tt);
//       }
//       break;
//     case MusECore::Pos::TICKS:
//       return MusECore::Pos(AL::sigmap.raster(pos.tick(), _raster), tt); 
//       break;
//   }  
//   
//   return pos;
// }
// 
// MusECore::Pos MidiEditor::rasterVal1(MusECore::Pos pos) const
// { 
//   MusECore::Pos::TType tt = canvas->timeType();
//   pos.setType(tt);
//   switch(tt)
//   {
//     case MusECore::Pos::FRAMES:
//       {
//         if(_raster == 0 || _raster == 1)
//           return pos;
//         // TODO: Probably incorrect. Fix proper snapping like sigmap.  // REMOVE Tim.
//         unsigned f = pos.frame();
//         unsigned r = f % _raster;
//         if(r >= _raster / 2)
//           return MusECore::Pos(f - r + _raster, tt);
//         else
//           return MusECore::Pos(f - r, tt);
//       }
//       break;
//     case MusECore::Pos::TICKS:
//       return MusECore::Pos(AL::sigmap.raster1(pos.tick(), _raster), tt);
//       break;
//   }  
//   
//   return pos;
// }
// 
// MusECore::Pos MidiEditor::rasterVal2(MusECore::Pos pos) const
// { 
//   MusECore::Pos::TType tt = canvas->timeType();
//   pos.setType(tt);
//   switch(tt)
//   {
//     case MusECore::Pos::FRAMES:
//       {
//         if(_raster == 0 || _raster == 1)
//           return pos;
//         // TODO: Probably incorrect. Fix proper snapping like sigmap.  // REMOVE Tim.
//         unsigned f = pos.frame();
//         unsigned r = f % _raster;
//         if(r >= _raster / 2)
//           return MusECore::Pos(f - r + _raster, tt);
//         else
//           return MusECore::Pos(f - r, tt);
//       }
//       break;
//     case MusECore::Pos::TICKS:
//       return MusECore::Pos(AL::sigmap.raster2(pos.tick(), _raster), tt); 
//       break;
//   }  
//   
//   return pos;
// }
// 
// MusECore::Pos MidiEditor::rasterSnapDown(MusECore::Pos pos) const
// {
//   MusECore::Pos::TType tt = canvas->timeType();
//   pos.setType(tt);
//   switch(tt)
//   {
//     case MusECore::Pos::FRAMES:
//       {
//         if(_raster == 0 || _raster == 1)
//           return pos;
//         // TODO: Probably incorrect. Fix proper snapping like sigmap.  // REMOVE Tim.
//         unsigned f = pos.frame();
//         unsigned r = f % _raster;
//         return MusECore::Pos(f - r, tt);
//       }
//       break;
//     case MusECore::Pos::TICKS:
//       {
//         int spos = pos.tick();
//         if(spos > 0) 
//         {
//           spos -= 1;     // Nudge by -1, then snap down with raster1.
//           spos = AL::sigmap.raster1(spos, rasterStep(pos));
//         }  
//         if(spos < 0)
//           spos = 0;
//         return MusECore::Pos(spos, tt);
//       }
//       break;
//   }  
//   
//   return pos;
// }
// 
// MusECore::Pos MidiEditor::rasterSnapUp(MusECore::Pos pos) const
// {
//   MusECore::Pos::TType tt = canvas->timeType();
//   pos.setType(tt);
//   switch(tt)
//   {
//     case MusECore::Pos::FRAMES:
//       {
//         if(_raster == 0 || _raster == 1)
//           return pos;
//         // TODO: Probably incorrect. Fix proper snapping like sigmap.  // REMOVE Tim.
//         unsigned f = pos.frame();
//         unsigned r = f % _raster;
//         if(r >= _raster / 2)
//           return MusECore::Pos(f - r + _raster, tt);
//         else
//           return MusECore::Pos(f - r, tt);
//       }
//       break;
//     case MusECore::Pos::TICKS:
//       // Nudge by +1, then snap up with raster2.
//       return MusECore::Pos(AL::sigmap.raster2(pos.tick() + 1, rasterStep(pos)), tt); 
//       break;
//   }  
//   
//   return pos;
// }
//  
// MusECore::Pos MidiEditor::rasterDownNoSnap(MusECore::Pos pos) const
// {
//   MusECore::Pos::TType tt = canvas->timeType();
//   pos.setType(tt);
//   MusECore::Pos rstep = rasterStep(pos);
//   if(pos <= rstep)
//     return MusECore::Pos(0, tt);
//   return pos - rstep;
// }
//  
// MusECore::Pos MidiEditor::rasterUpNoSnap(MusECore::Pos pos) const
// {
//   MusECore::Pos::TType tt = canvas->timeType();
//   pos.setType(tt);
//   return pos + rasterStep(pos);
// }

// REMOVE Tim.
// MusECore::Pos MidiEditor::raster() const
// { 
//   return MusECore::Pos(_raster, canvas->timeType()); 
// }

void MidiEditor::horizontalZoomIn()
{
  int mag = hscroll->mag();
  int zoomlvl = MusEGui::ScrollScale::getQuickZoomLevel(mag);
  if (zoomlvl < MusEGui::ScrollScale::zoomLevels-1)
        zoomlvl++;

  int newmag = MusEGui::ScrollScale::convertQuickZoomLevelToMag(zoomlvl);

  hscroll->setMag(newmag);

}

void MidiEditor::horizontalZoomOut()
{
  int mag = hscroll->mag();
  int zoomlvl = MusEGui::ScrollScale::getQuickZoomLevel(mag);
  if (zoomlvl > 1)
        zoomlvl--;

  int newmag = MusEGui::ScrollScale::convertQuickZoomLevelToMag(zoomlvl);

  hscroll->setMag(newmag);

}

void MidiEditor::addNewParts(const std::map< MusECore::Part*, std::set<MusECore::Part*> >& param)
{
  using std::map;
  using std::set;
  
  for (map< MusECore::Part*, set<MusECore::Part*> >::const_iterator it = param.begin(); it!=param.end(); it++)
    if (_pl->index(it->first) != -1)
      for (set<MusECore::Part*>::const_iterator it2=it->second.begin(); it2!=it->second.end(); it2++)
        addPart(*it2);
}

} // namespace MusEGui
