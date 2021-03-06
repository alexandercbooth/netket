// Copyright 2018 The Simons Foundation, Inc. - All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef NETKET_METROPOLISLOCAL_HH
#define NETKET_METROPOLISLOCAL_HH

#include <iostream>
#include <Eigen/Dense>
#include <random>
#include <mpi.h>
#include <limits>

namespace netket{

using namespace std;
using namespace Eigen;

//Metropolis sampling generating local moves in hilbert space
template<class WfType> class MetropolisLocal: public AbstractSampler<WfType>{

  WfType & psi_;

  const Hilbert & hilbert_;

  //number of visible units
  const int nv_;

  netket::default_random_engine rgen_;

  //states of visible units
  VectorXd v_;

  VectorXd accept_;
  VectorXd moves_;

  int mynode_;
  int totalnodes_;

  //Look-up tables
  typename WfType::LookupType lt_;


  int nstates_;
  vector<double> localstates_;


public:

  MetropolisLocal(WfType & psi):
       psi_(psi),hilbert_(psi.GetHilbert()),nv_(hilbert_.Size()){
    Init();
  }

  //Json constructor
  MetropolisLocal(Graph & graph,WfType & psi,const json & pars):
    psi_(psi),hilbert_(psi.GetHilbert()),nv_(hilbert_.Size()){
    Init();
  }

  void Init(){
    v_.resize(nv_);

    MPI_Comm_size(MPI_COMM_WORLD, &totalnodes_);
    MPI_Comm_rank(MPI_COMM_WORLD, &mynode_);

    if(!hilbert_.IsDiscrete()){
      if(mynode_==0){
        cerr<<"# Local Metropolis sampler works only for discrete Hilbert spaces"<<endl;
      }
      std::abort();
    }

    accept_.resize(1);
    moves_.resize(1);

    nstates_=hilbert_.LocalSize();
    localstates_=hilbert_.LocalStates();

    Seed();

    Reset(true);


    if(mynode_==0){
      cout<<"# Local Metropolis sampler is ready "<<endl;
    }
  }

  void Seed(int baseseed=0){
    std::random_device rd;
    vector<int> seeds(totalnodes_);

    if(mynode_==0){
      for(int i=0;i<totalnodes_;i++){
        seeds[i]=rd()+baseseed;
      }
    }

    SendToAll(seeds);

    rgen_.seed(seeds[mynode_]);
  }


  void Reset(bool initrandom=false){
    if(initrandom){
      hilbert_.RandomVals(v_,rgen_);
    }

    psi_.InitLookup(v_,lt_);

    accept_=VectorXd::Zero(1);
    moves_=VectorXd::Zero(1);
  }

  void Sweep(){

    vector<int> tochange(1);
    vector<double> newconf(1);

    std::uniform_real_distribution<double> distu;
    std::uniform_int_distribution<int> distrs(0,nv_-1);
    std::uniform_int_distribution<int> diststate(0,nstates_-1);

    for(int i=0;i<nv_;i++){

      //picking a random site to be changed
      int si=distrs(rgen_);
      assert(si<nv_);
      tochange[0]=si;

      //picking a random state
      int newstate=diststate(rgen_);
      newconf[0]=localstates_[newstate];

      //make sure that the new state is not equal to the current one
      while(std::abs(newconf[0]-v_(si))<std::numeric_limits<double>::epsilon() ){
        newstate=diststate(rgen_);
        newconf[0]=localstates_[newstate];
      }

      const auto lvd=psi_.LogValDiff(v_,tochange,newconf,lt_);
      double ratio=std::norm(std::exp(lvd));

      #ifndef NDEBUG
      const auto psival1=psi_.LogVal(v_);
      if(std::abs(std::exp(psi_.LogVal(v_)-psi_.LogVal(v_,lt_))-1.)>1.0e-8){
        std::cerr<<psi_.LogVal(v_)<<"  and LogVal with Lt is "<<psi_.LogVal(v_,lt_)<<std::endl;
        std::abort();
      }
      #endif

      //Metropolis acceptance test
      if(ratio>distu(rgen_)){
        accept_[0]+=1;
        psi_.UpdateLookup(v_,tochange,newconf,lt_);
        hilbert_.UpdateConf(v_,tochange,newconf);

        #ifndef NDEBUG
        const auto psival2=psi_.LogVal(v_);
        if(std::abs(std::exp(psival2-psival1-lvd)-1.)>1.0e-8){
          std::cerr<<psival2-psival1<<" and logvaldiff is "<<lvd<<std::endl;
          std::cerr<<psival2<<" and LogVal with Lt is "<<psi_.LogVal(v_,lt_)<<std::endl;
          std::abort();
        }
        #endif
      }
      moves_[0]+=1;
    }
  }


  VectorXd Visible(){
    return v_;
  }

  void SetVisible(const VectorXd & v){
    v_=v;
  }


  WfType & Psi(){
    return psi_;
  }

  Hilbert & HilbSpace()const{
    return hilbert_;
  }

  VectorXd Acceptance()const{
    VectorXd acc=accept_;
    for(int i=0;i<1;i++){
      acc(i)/=moves_(i);
    }
    return acc;
  }

};


}

#endif
