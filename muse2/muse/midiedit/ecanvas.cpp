//=========================================================
//  MusE
//  Linux Music Editor
//    $Id: ecanvas.cpp,v 1.8.2.6 2009/05/03 04:14:00 terminator356 Exp $
//  (C) Copyright 2001 Werner Schweer (ws@seh.de)
//  (C) Copyright 2011 Tim E. Real (terminator356 on sourceforge)
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

#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <QKeyEvent>
#include <QDropEvent>
#include <QEvent>
#include <QMimeData>
#include <QByteArray>
#include <QDrag>

#include "xml.h"
#include "midieditor.h"
#include "ecanvas.h"
#include "song.h"
#include "event.h"
#include "shortcuts.h"
#include "audio.h"
#include "functions.h"

namespace MusEGui {

//---------------------------------------------------------
//   EventCanvas
//---------------------------------------------------------

EventCanvas::EventCanvas(MidiEditor* pr, QWidget* parent, int sx, int sy, const char* name)
//EventCanvas::EventCanvas(MidiEditor* pr, QWidget* parent, int sx,
//   int sy, const char* name, MusECore::Pos::TType time_type, bool formatted, int raster)
   : Canvas(parent, sx, sy, name)
//   : Canvas(parent, sx, sy, name, time_type, formatted, raster)
      {
      _editor      = pr;
      _steprec    = false;
      _midiin     = false;
      _playEvents = false;
      _setCurPartIfOnlyOneEventIsSelected = true;
      curVelo     = 70;

      setBg(Qt::white);
      setAcceptDrops(true);
      setFocusPolicy(Qt::StrongFocus);
      setMouseTracking(true);

      curPart   = (MusECore::MidiPart*)(_editor->parts()->begin()->second);
      curPartId = curPart->sn();
      connect(MusEGlobal::song, SIGNAL(posChanged(int, const MusECore::Pos&, bool)), this, SLOT(setPos(int, const MusECore::Pos&, bool)));
      }

//---------------------------------------------------------
//   getCaption
//---------------------------------------------------------

QString EventCanvas::getCaption() const
      {
      int bar1, bar2, xx;
      unsigned x;
      AL::sigmap.tickValues(curPart->tick(), &bar1, &xx, &x);
      AL::sigmap.tickValues(curPart->tick() + curPart->lenTick(), &bar2, &xx, &x);

      return QString("MusE: Part <") + curPart->name()
         + QString("> %1-%2").arg(bar1+1).arg(bar2+1);
      }

//---------------------------------------------------------
//   leaveEvent
//---------------------------------------------------------

void EventCanvas::leaveEvent(QEvent*)
      {
      emit pitchChanged(-1);
      emit timeChanged(INT_MAX);   // REMOVE Tim. When conversion to all Pos is done.
      emit timeChanged(MusECore::Pos(true)); // a null position
      }

//---------------------------------------------------------
//   enterEvent
//---------------------------------------------------------

void EventCanvas::enterEvent(QEvent*)
      {
      emit enterCanvas();
      }

//---------------------------------------------------------
//   setPos
//---------------------------------------------------------

void EventCanvas::setPos(int idx, const MusECore::Pos& val, bool adjustScrollbar)
{
  Canvas::setPos(idx, val, adjustScrollbar, _editor->rasterizer());  
}

//---------------------------------------------------------
//   draw
//---------------------------------------------------------

void EventCanvas::draw(QPainter& p, const QRect& rect)
{
  Canvas::draw(p, rect, _editor->rasterizer());  
}

//---------------------------------------------------------
//   raster
//---------------------------------------------------------

QPoint EventCanvas::rasterPoint(const QPoint& p) const
      {
      int x = p.x();
      if (x < 0)
            x = 0;
      //x = editor->rasterVal(x);
      //x = editor->rasterVal(MusECore::Pos(x, _timeType)).value(_timeType);
      const MusECore::Rasterizer& rast = _editor->rasterizer();      
      x = rast.rasterVal(MusECore::Pos(x, rast.timeType())).posValue(rast.timeType());
      int pitch = y2pitch(p.y());
      int y = pitch2y(pitch);
      return QPoint(x, y);
      }

//---------------------------------------------------------
//   mouseMove
//---------------------------------------------------------

void EventCanvas::mouseMove(QMouseEvent* event)
      {
      emit pitchChanged(y2pitch(event->pos().y()));
      int x = event->pos().x();
      const MusECore::Rasterizer& rast = _editor->rasterizer();      
      //emit timeChanged(editor->rasterVal(x));   // REMOVE Tim. When conversion to all Pos is done.
      emit timeChanged(rast.rasterVal(x));   // REMOVE Tim. When conversion to all Pos is done.
      emit timeChanged(MusECore::Pos(rast.rasterVal(x), rast.timeType()));
      }

//---------------------------------------------------------
//   updateSelection
//---------------------------------------------------------

void EventCanvas::updateSelection()
      {
      MusEGlobal::song->update(SC_SELECTION);
      }

//---------------------------------------------------------
//   songChanged(type)
//---------------------------------------------------------

void EventCanvas::songChanged(MusECore::SongChangedFlags_t flags)
      {
      // Is it simply a midi controller value adjustment? Forget it.
      if(flags == SC_MIDI_CONTROLLER)
        return;
    
      if (flags & ~SC_SELECTION) {
            // TODO FIXME: don't we actually only want SC_PART_*, and maybe SC_TRACK_DELETED?
            //             (same in waveview.cpp)
            bool curItemNeedsRestore=false;
            MusECore::Event storedEvent;
            int partSn;
            if (curItem)
            {
              curItemNeedsRestore=true;
              storedEvent=curItem->event();
              partSn=curItem->part()->sn();
            }
            curItem=NULL;
            
            items.clearDelete();
            //start_tick  = INT_MAX;
            //end_tick    = 0;
            _startPos.setType(MusECore::Pos::TICKS);
            _endPos.setType(MusECore::Pos::TICKS);
            _startPos.setNullFlag(true);
            _endPos.setTick(0);
            curPart = 0;
            for (MusECore::iPart p = _editor->parts()->begin(); p != _editor->parts()->end(); ++p) {
                  MusECore::MidiPart* part = (MusECore::MidiPart*)(p->second);
                  if (part->sn() == curPartId)
                        curPart = part;
                  unsigned stick = part->tick();
                  unsigned len = part->lenTick();
                  unsigned etick = stick + len;
//                   if (stick < start_tick)
//                         start_tick = stick;
//                   if (etick > end_tick)
//                         end_tick = etick;
                  if (stick < _startPos.tick())
                        _startPos.setTick(stick); 
                  if (etick > _endPos.tick())
                        _endPos.setTick(etick);

                  MusECore::EventList* el = part->events();
                  for (MusECore::iEvent i = el->begin(); i != el->end(); ++i) {
                        MusECore::Event e = i->second;
                        // Do not add events which are past the end of the part.
                        if(e.tick() > len)      
                          break;
                        
                        if (e.isNote()) {
                              CItem* temp = addItem(part, e);
                              
                              if (temp && curItemNeedsRestore && e==storedEvent && part->sn()==partSn)
                              {
                                  if (curItem!=NULL)
                                    printf("THIS SHOULD NEVER HAPPEN: curItemNeedsRestore=true, event fits, but there was already a fitting event!?\n");
                                  
                                  curItem=temp;
                                  }
                              }
                        }
                  }
            }

      MusECore::Event event;
      MusECore::MidiPart* part   = 0;
      int x            = 0;
      CItem*   nevent  = 0;

      int n  = 0;       // count selections
      for (iCItem k = items.begin(); k != items.end(); ++k) {
            MusECore::Event ev = k->second->event();
            bool selected = ev.selected();
            if (selected) {
                  k->second->setSelected(true);
                  ++n;
                  if (!nevent) {
                        nevent   =  k->second;
                        MusECore::Event mi = nevent->event();
                        curVelo  = mi.velo();
                        }
                  }
            }
            
      //start_tick = MusEGlobal::song->roundDownBar(start_tick);
      //end_tick   = MusEGlobal::song->roundUpBar(end_tick);
      _startPos.setTick(MusEGlobal::song->roundDownBar(_startPos.tick()));
      _endPos.setTick(MusEGlobal::song->roundUpBar(_endPos.tick()));

      if (n >= 1)    
      {
            x     = nevent->x();
            event = nevent->event();
            part  = (MusECore::MidiPart*)nevent->part();
            if (_setCurPartIfOnlyOneEventIsSelected && n == 1 && curPart != part) {
                  curPart = part;
                  curPartId = curPart->sn();
                  curPartChanged();
                  }
      }
      
      bool f1 = flags & (SC_EVENT_INSERTED | SC_EVENT_MODIFIED | SC_EVENT_REMOVED | 
                         SC_PART_INSERTED | SC_PART_MODIFIED | SC_PART_REMOVED |
                         SC_TRACK_INSERTED | SC_TRACK_REMOVED | SC_TRACK_MODIFIED |
                         SC_SIG | SC_TEMPO | SC_KEY | SC_MASTER | SC_CONFIG | SC_DRUMMAP); 
      bool f2 = flags & SC_SELECTION;
      if(f1 || f2)   // Try to avoid all unnecessary emissions.
        emit selectionChanged(x, event, part, !f1);
      
      if (curPart == 0)
            curPart = (MusECore::MidiPart*)(_editor->parts()->begin()->second);
      redraw();
      }

//---------------------------------------------------------
//   selectAtTick
//---------------------------------------------------------
void EventCanvas::selectAtTick(unsigned int tick)
      {
      //Select note nearest tick, if none selected and there are any
      if (!items.empty() && selectionSize() == 0) {
            iCItem i = items.begin();
	    CItem* nearest = i->second;

            while (i != items.end()) {
                CItem* cur=i->second;                
                unsigned int curtk=abs(cur->x() + cur->part()->tick() - tick);
                unsigned int neartk=abs(nearest->x() + nearest->part()->tick() - tick);

                if (curtk < neartk) {
                    nearest=cur;
                    }

                i++;
                }

            if (!nearest->isSelected()) {
                  selectItem(nearest, true);
                  songChanged(SC_SELECTION);
                  }
            }
      }

//---------------------------------------------------------
//   track
//---------------------------------------------------------

MusECore::MidiTrack* EventCanvas::track() const
      {
      return ((MusECore::MidiPart*)curPart)->track();
      }


//---------------------------------------------------------
//   keyPress
//---------------------------------------------------------

void EventCanvas::keyPress(QKeyEvent* event)
      {
      int key = event->key();
      if (((QInputEvent*)event)->modifiers() & Qt::ShiftModifier)
            key += Qt::SHIFT;
      if (((QInputEvent*)event)->modifiers() & Qt::AltModifier)
            key += Qt::ALT;
      if (((QInputEvent*)event)->modifiers() & Qt::ControlModifier)
            key+= Qt::CTRL;

      //
      //  Shortcut for DrumEditor & PianoRoll
      //  Sets locators to selected events
      //
      if (key == shortcuts[SHRT_LOCATORS_TO_SELECTION].key) {
            int tick_max = 0;
            int tick_min = INT_MAX;
            bool found = false;

            for (iCItem i= items.begin(); i != items.end(); i++) {
                  if (!i->second->isSelected())
                        continue;

                  int tick = i->second->x();
                  int len = i->second->event().lenTick();
                  found = true;
                  if (tick + len > tick_max)
                        tick_max = tick + len;
                  if (tick < tick_min)
                        tick_min = tick;
                  }
            if (found) {
                  MusECore::Pos p1(tick_min, true);
                  MusECore::Pos p2(tick_max, true);
                  MusEGlobal::song->setPos(1, p1);
                  MusEGlobal::song->setPos(2, p2);
                  }
            }
      // Select items by key (PianoRoll & DrumEditor)
      else if (key == shortcuts[SHRT_SEL_RIGHT].key || key == shortcuts[SHRT_SEL_RIGHT_ADD].key) {
              rciCItem i;

              if (items.empty())
                  return;
              for (i = items.rbegin(); i != items.rend(); ++i) 
                if (i->second->isSelected()) 
                  break;

              if(i == items.rend())
                i = items.rbegin();
              
              if(i != items.rbegin())
                --i;
              if(i->second)
              {
                if (key != shortcuts[SHRT_SEL_RIGHT_ADD].key)
                      deselectAll();
                CItem* sel = i->second;
                sel->setSelected(true);
                updateSelection();
                if (sel->x() + sel->width() > mapxDev(width())) 
                {  
                  int mx = rmapx(sel->x());  
                  int newx = mx + rmapx(sel->width()) - width();
                  // Leave a bit of room for the specially-drawn drum notes. But good for piano too.
                  emit horizontalScroll( (newx > mx ? mx - 10: newx + 10) - rmapx(xorg) );
                }  
              }
            }
      //Select items by key: (PianoRoll & DrumEditor)
      else if (key == shortcuts[SHRT_SEL_LEFT].key || key == shortcuts[SHRT_SEL_LEFT_ADD].key) {
              ciCItem i;
              if (items.empty())
                  return;
              for (i = items.begin(); i != items.end(); ++i)
                if (i->second->isSelected()) 
                  break;

              if(i == items.end())
                i = items.begin();
              
              if(i != items.begin())
                --i;
              if(i->second)
              {
                if (key != shortcuts[SHRT_SEL_LEFT_ADD].key)
                      deselectAll();
                CItem* sel = i->second;
                sel->setSelected(true);
                updateSelection();
                if (sel->x() <= mapxDev(0)) 
                  emit horizontalScroll(rmapx(sel->x() - xorg) - 10);  // Leave a bit of room.
              }
            }
      else if (key == shortcuts[SHRT_INC_PITCH].key) {
            modifySelected(NoteInfo::VAL_PITCH, 1);
            }
      else if (key == shortcuts[SHRT_DEC_PITCH].key) {
            modifySelected(NoteInfo::VAL_PITCH, -1);
            }
      else if (key == shortcuts[SHRT_INC_POS].key) {
            // TODO: Check boundaries
            modifySelected(NoteInfo::VAL_TIME, _editor->rasterizer().raster());
            }
      else if (key == shortcuts[SHRT_DEC_POS].key) {
            // TODO: Check boundaries
            modifySelected(NoteInfo::VAL_TIME, 0 - _editor->rasterizer().raster());
            }

      else if (key == shortcuts[SHRT_INCREASE_LEN].key) {
            // TODO: Check boundaries
            modifySelected(NoteInfo::VAL_LEN, _editor->rasterizer().raster());
            }
      else if (key == shortcuts[SHRT_DECREASE_LEN].key) {
            // TODO: Check boundaries
            modifySelected(NoteInfo::VAL_LEN, 0 - _editor->rasterizer().raster());
            }

      else
            event->ignore();
      }


//---------------------------------------------------------
//   dropEvent
//---------------------------------------------------------

void EventCanvas::viewDropEvent(QDropEvent* event)
      {
      QString text;
      if (event->source() == this) {
            printf("local DROP\n");      
            //event->acceptProposedAction();     
            //event->ignore();                     // TODO CHECK Tim.
            return;
            }
      if (event->mimeData()->hasFormat("text/x-muse-groupedeventlists")) {
            text = QString(event->mimeData()->data("text/x-muse-groupedeventlists"));
      
            //int x = editor->rasterVal(event->pos().x());  // REMOVE Tim.
            const MusECore::Rasterizer& rast = _editor->rasterizer();      
            //int x = editor->rasterVal(MusECore::Pos(event->pos().x(), _timeType)).value(_timeType);
            int x = rast.rasterVal(MusECore::Pos(event->pos().x(), rast.timeType())).posValue(rast.timeType());
            if (x < 0)
                  x = 0;
            paste_at(text,x,3072,false,false,curPart);
            //event->accept();  // TODO
            }
      else {
            printf("cannot decode drop\n");
            //event->acceptProposedAction();     
            //event->ignore();                     // TODO CHECK Tim.
            }
      }


//---------------------------------------------------------
//   endMoveItems
//    dir = 0     move in all directions
//          1     move only horizontal
//          2     move only vertical
//---------------------------------------------------------

void EventCanvas::endMoveItems(const QPoint& pos, DragType dragtype, int dir)
      {
      int dp = y2pitch(pos.y()) - y2pitch(Canvas::start.y());
      int dx = pos.x() - Canvas::start.x();

      if (dir == 1)
            dp = 0;
      else if (dir == 2)
            dx = 0;
      
      
      
      MusECore::Undo operations = moveCanvasItems(moving, dp, dx, dragtype);
      if (operations.empty())
        songChanged(SC_EVENT_MODIFIED); //this is a hack to force the canvas to repopulate
      	                                //itself. otherwise, if a moving operation was forbidden,
      	                                //the canvas would still show the movement
      else
        MusEGlobal::song->applyOperationGroup(operations);
      
      moving.clear();
      updateSelection();
      redraw();
      }

} // namespace MusEGui
