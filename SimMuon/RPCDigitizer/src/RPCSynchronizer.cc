#include "Geometry/RPCGeometry/interface/RPCRoll.h"
#include "Geometry/RPCGeometry/interface/RPCRollSpecs.h"
#include "SimMuon/RPCDigitizer/src/RPCSynchronizer.h"
#include "Geometry/CommonTopologies/interface/RectangularStripTopology.h"
#include "Geometry/CommonTopologies/interface/TrapezoidalStripTopology.h"

#include <FWCore/Framework/interface/Frameworkfwd.h>
#include <FWCore/Framework/interface/EventSetup.h>
#include <FWCore/Framework/interface/EDAnalyzer.h>
#include <FWCore/Framework/interface/Event.h>
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include <FWCore/Framework/interface/ESHandle.h>

#include "SimDataFormats/TrackingHit/interface/PSimHitContainer.h"
#include "SimDataFormats/TrackingHit/interface/PSimHit.h"
#include "Geometry/RPCGeometry/interface/RPCGeometry.h"
#include <Geometry/Records/interface/MuonGeometryRecord.h>
#include "DataFormats/MuonDetId/interface/RPCDetId.h"

#include "SimDataFormats/TrackingHit/interface/PSimHit.h"

#include "FWCore/ServiceRegistry/interface/Service.h" 
#include "FWCore/Utilities/interface/RandomNumberGenerator.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "CLHEP/Random/RandomEngine.h"
#include "CLHEP/Random/RandFlat.h"
#include <CLHEP/Random/RandGaussQ.h>
#include <CLHEP/Random/RandFlat.h>

#include<cstring>
#include<iostream>
#include<fstream>
#include<string>
#include<vector>
#include<stdlib.h>
#include <cmath>

using namespace std;

RPCSynchronizer::RPCSynchronizer(const edm::ParameterSet& config){

  resRPC = config.getParameter<double>("timeResolution");  //intrinsic RPC time resolution - units: ns             ---> rtim_res1
  timOff = config.getParameter<double>("timingRPCOffset");  /* RPC time off-set.It takes into                      ---> rpc_time_offset
							     account the average time for the
							     detector to respond and possible
							     delay time due to cables */
  dtimCs = config.getParameter<double>("deltatimeAdjacentStrip");  // parameter for delay of the strips of cluster ---> rpc_csdt
  resEle = config.getParameter<double>("timeJitter");  //jitter of the RPC electronics - units: ns                 ---> rtim_res2
  sspeed = config.getParameter<double>("signalPropagationSpeed");   //units cm/ns                                  ---> prop_speed
  lbGate = config.getParameter<double>("linkGateWidth");  //time gate width for the RPC signals                    ---> rpc_gate

  file = config.getParameter<bool>("file");
  cosmics = config.getParameter<bool>("cosmics");

  edm::FileInPath fp = config.getParameter<edm::FileInPath>("timingMap");
  filename=fp.fullPath();

  _bxmap.clear();
}

void RPCSynchronizer::setReadOutTime(const RPCGeometry* geo){
  
  theGeometry = geo;

  if(file){
    int detUnit = 0;
    float timing = 0.;

    infile = new fstream(filename.c_str(),std::ios::in);
    
    int i = 0;
    while(!infile->eof()){
      i++;
      *infile>>detUnit>>timing;

      _bxmap[RPCDetId(static_cast<uint32_t>(detUnit))] = timing;
    }
    infile->close();
  }
  else{

    for(TrackingGeometry::DetContainer::const_iterator it = theGeometry->dets().begin(); it != theGeometry->dets().end(); it++){
    
      if( dynamic_cast< RPCChamber* >( *it ) != 0 ){
	
	RPCChamber* ch = dynamic_cast< RPCChamber* >( *it ); 
	RPCDetId detId=ch->id();
	
	std::vector< const RPCRoll*> rollsRaf = (ch->rolls());
	for(std::vector<const RPCRoll*>::iterator r = rollsRaf.begin();
	    r != rollsRaf.end(); ++r){
	  
	  const BoundPlane & RPCSurface = (*r)->surface(); 
	  GlobalPoint CenterPointRollGlobal = RPCSurface.toGlobal(LocalPoint(0,0,0));
	  float space = CenterPointRollGlobal.mag();

	  float time = space/(3e+10);
	  
	  _bxmap[(*r)->id()] = time*1e+9;
	  
	}
      }
    }
  }
}

float RPCSynchronizer::getReadOutTime(const RPCDetId& rpcDetId)
{
  std::map<RPCDetId, float >::iterator it = _bxmap.find(rpcDetId);
  return it->second;
}

int RPCSynchronizer::getSimHitBx(const PSimHit* simhit)
{

  int bx = -999;
  LocalPoint simHitPos = simhit->localPosition();
  float tof = simhit->timeOfFlight();
  float rr_el = RandGaussQ::shoot(0.,resEle);

  RPCDetId SimDetId(simhit->detUnitId());

  const RPCRoll* SimRoll = 0;

  for(TrackingGeometry::DetContainer::const_iterator it = theGeometry->dets().begin(); it != theGeometry->dets().end(); it++){
    
    if( dynamic_cast< RPCChamber* >( *it ) != 0 ){
      
      RPCChamber* ch = dynamic_cast< RPCChamber* >( *it ); 
      RPCDetId detId=ch->id();
      
      std::vector< const RPCRoll*> rollsRaf = (ch->rolls());
      for(std::vector<const RPCRoll*>::iterator r = rollsRaf.begin();
	  r != rollsRaf.end(); ++r){

	  if((*r)->id() == SimDetId) {
	    SimRoll = &(*(*r));
	    const BoundPlane & RPCSurface = (*r)->surface(); 
	    GlobalPoint CenterPointRollGlobal = RPCSurface.toGlobal(LocalPoint(0,0,0));
	    break;
	  }
      }
    }
  }

  if(SimRoll != 0){

    float distanceFromEdge = 0;
    float stripL = 0.;

    if(SimRoll->id().region() == 0){
      const RectangularStripTopology* top_= dynamic_cast<const RectangularStripTopology*> (&(SimRoll->topology()));
      distanceFromEdge = top_->stripLength() - simHitPos.y();
      stripL = top_->stripLength();
    }else{
      const TrapezoidalStripTopology* top_= dynamic_cast<const TrapezoidalStripTopology*> (&(SimRoll->topology()));
      distanceFromEdge = top_->stripLength() - simHitPos.y();
      stripL = top_->stripLength();
    }

    float prop_time =  distanceFromEdge/(sspeed*3e+10);
    double rr_tim1 = RandGaussQ::shoot(0.,resRPC);
    double total_time = tof + prop_time + timOff + rr_tim1 + rr_el;

    // Bunch crossing assignment
    double time_differ = 0.;

    if(cosmics){
      time_differ = total_time/37.62 - ( this->getReadOutTime(RPCDetId(simhit->detUnitId())) + ((stripL/(2*sspeed*3e+10) ) + timOff)/37.62);
    }
    else if(!cosmics){
      time_differ = total_time - ( this->getReadOutTime(RPCDetId(simhit->detUnitId())) + ( stripL/(2*sspeed*3e+10) ) + timOff);
    }

    for(int n = -5; n <= 5; ++n){
      double inf_time = 0;
      double sup_time = 0;

      if(cosmics){
	inf_time = -lbGate/(2*37.62) + n*lbGate/37.62;
	sup_time = lbGate/(2*37.62) + n*lbGate/37.62;
      }
      else if(!cosmics){
	inf_time = -lbGate/2 + n*lbGate;
	sup_time = lbGate/2 + n*lbGate;
      }

      if(inf_time < time_differ && time_differ < sup_time) {
	bx = n;
	break;
      }
    }
  }

  return bx;
}

int RPCSynchronizer::getDigiBx(const PSimHit* simhit, int centralstrip, int strip)
{

  unsigned int diffstrip = abs(strip - centralstrip);

  float csdt_tot = 0.;
  if(diffstrip > 0){
    for(unsigned int n = 0; n < diffstrip; ++n){
      float rr_dt = RandFlat::shoot();
      if (rr_dt <= 1.e-10) rr_dt = 1.e-10 ;
      float dif_time = -(dtimCs)*log(rr_dt);
      csdt_tot += dif_time;
    }
  }

  float rr_el = RandGaussQ::shoot(0.,resEle);

  int bx = -999;
  LocalPoint simHitPos = simhit->localPosition();
  float tof = simhit->timeOfFlight();

  RPCDetId SimDetId(simhit->detUnitId());
  const RPCRoll* SimRoll = 0;

  for(TrackingGeometry::DetContainer::const_iterator it = theGeometry->dets().begin(); it != theGeometry->dets().end(); it++){
    
    if( dynamic_cast< RPCChamber* >( *it ) != 0 ){
      
      RPCChamber* ch = dynamic_cast< RPCChamber* >( *it ); 
      RPCDetId detId=ch->id();
      
      std::vector< const RPCRoll*> rollsRaf = (ch->rolls());
      for(std::vector<const RPCRoll*>::iterator r = rollsRaf.begin();
	  r != rollsRaf.end(); ++r){
	
	  if((*r)->id() == SimDetId) {
	    SimRoll = &(*(*r));
	  
	    const BoundPlane & RPCSurface = (*r)->surface(); 
	    GlobalPoint CenterPointRollGlobal = RPCSurface.toGlobal(LocalPoint(0,0,0));
	    break;
	  }	  
      }
    }
  }

  if(SimRoll != 0){
    
    const RectangularStripTopology* top_= dynamic_cast<const RectangularStripTopology*> (&(SimRoll->topology()));
    float distanceFromEdge = top_->stripLength() - simHitPos.y();

    float prop_time =  distanceFromEdge/(sspeed*3e+10);
    double rr_tim1 = RandGaussQ::shoot(0.,resRPC);
    double total_time = tof + prop_time + timOff + rr_tim1 + rr_el + csdt_tot;

    // Bunch crossing assignment

    double time_differ = total_time - ( this->getReadOutTime(RPCDetId(simhit->detUnitId())) + ( top_->stripLength()/(2*sspeed*3e+10) ) + timOff);

    for(int n = -5; n <= 5; n++){

      float inf_time = -lbGate/2 + n*lbGate;
      float sup_time = lbGate/2 + n*lbGate;

      if(inf_time < time_differ && time_differ < sup_time) {
	bx = n;
	break;
      }
    }
  }

  return bx;
}

RPCSynchronizer::~RPCSynchronizer(){
  delete theGeometry;
  if(infile != 0) delete infile;
}


