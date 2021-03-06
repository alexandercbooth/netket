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

#ifndef NETKET_HILBERT_CC
#define NETKET_HILBERT_CC

namespace netket{

class Hilbert:public AbstractHilbert{

  AbstractHilbert* h_;

public:

  Hilbert(){

  }

  Hilbert(const json & pars){
    Init(pars);
  }

  void Init(const json & pars){
    if(!FieldExists(pars,"Hilbert")){
      cerr<<"Hilbert is not defined in the input"<<endl;
      std::abort();
    }

    if(FieldExists(pars["Hilbert"],"Name")){
      if(pars["Hilbert"]["Name"]=="Spin"){
        h_=new Spin(pars);
      }
      else if(pars["Hilbert"]["Name"]=="Boson"){
        h_=new Boson(pars);
      }
      else if(pars["Hilbert"]["Name"]=="Qubit"){
        h_=new Qubit(pars);
      }
      else{
        cout<<"Hilbert Name not found"<<endl;
        std::abort();
      }
    }
    else{
      h_=new CustomHilbert(pars);
    }
  }

  bool IsDiscrete()const{
    return h_->IsDiscrete();
  }

  int LocalSize()const{
    return h_->LocalSize();
  }

  int Size()const{
    return h_->Size();
  }

  vector<double> LocalStates()const{
    return h_->LocalStates();
  }

  void RandomVals(VectorXd & state,netket::default_random_engine & rgen)const{
    return h_->RandomVals(state,rgen);
  }

  void UpdateConf(VectorXd & v,const vector<int>  & tochange,
    const vector<double> & newconf)const{
    return h_->UpdateConf(v,tochange,newconf);
  }
};
}
#endif
