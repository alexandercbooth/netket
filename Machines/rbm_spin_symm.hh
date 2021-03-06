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

#include <iostream>
#include <Eigen/Dense>
#include <random>
#include <vector>

#ifndef NETKET_RBM_SPIN_SYMM_HH
#define NETKET_RBM_SPIN_SYMM_HH

namespace netket{

using namespace std;
using namespace Eigen;

//Rbm with permutation symmetries
template<typename T> class RbmSpinSymm: public AbstractMachine<T>{

  using VectorType=typename AbstractMachine<T>::VectorType;
  using MatrixType=typename AbstractMachine<T>::MatrixType;

  vector<vector<int>> permtable_;
  int permsize_;

  //number of visible units
  int nv_;

  //ratio of hidden/visible
  int alpha_;

  //number of hidden units
  int nh_;

  //number of parameters
  int npar_;

  //number of parameters without symmetries
  int nbarepar_;

  //weights
  MatrixType W_;

  //weights with symmetries
  MatrixType Wsymm_;

  //visible units bias
  VectorType a_;

  T asymm_;

  //hidden units bias
  VectorType b_;

  VectorType bsymm_;

  VectorType thetas_;
  VectorType lnthetas_;
  VectorType thetasnew_;
  VectorType lnthetasnew_;

  MatrixXd DerMatSymm_;


  bool usea_;
  bool useb_;

  int mynode_;

  const Hilbert & hilbert_;

  const Graph & graph_;

public:

  using StateType=typename AbstractMachine<T>::StateType;
  using LookupType=typename AbstractMachine<T>::LookupType;

  template<class Ham> RbmSpinSymm(int alpha,const Graph & graph,
    const Ham & hamiltonian,bool usea=true,bool useb=true):
  nv_(graph.Nsites()),alpha_(alpha),usea_(usea),useb_(useb),
  hilbert_(hamiltonian.GetHilbert()),graph_(graph){

    Init(graph);
  }

  //Json constructor
  RbmSpinSymm(const Graph & graph,const Hamiltonian<Graph> & hamiltonian,const json & pars):
    nv_(graph.Nsites()),
    hilbert_(hamiltonian.GetHilbert()),
    graph_(graph){

    from_json(pars);
  }

  template<class G> void Init(const G & graph){

    permtable_=graph.SymmetryTable();
    permsize_=permtable_.size();
    nh_=(alpha_*permsize_);

    for(int i=0;i<permsize_;i++){
      assert(permtable_[i].size()==nv_);
    }

    W_.resize(nv_,nh_);
    a_.resize(nv_);
    b_.resize(nh_);

    thetas_.resize(nh_);
    lnthetas_.resize(nh_);
    thetasnew_.resize(nh_);
    lnthetasnew_.resize(nh_);

    Wsymm_.resize(nv_,alpha_);
    bsymm_.resize(alpha_);

    npar_=nv_*alpha_;
    nbarepar_=nv_*nh_;

    if(usea_){
      npar_+=1;
      nbarepar_+=nv_;
    }
    else{
      asymm_=0;
      a_.setZero();
    }

    if(useb_){
      npar_+=alpha_;
      nbarepar_+=nh_;
    }
    else{
      bsymm_.setZero();
      b_.setZero();
    }

    //Constructing the matrix that maps the bare derivatives to the symmetric ones
    DerMatSymm_=MatrixXd::Zero(npar_,nbarepar_);

    int k=0;
    int kbare=0;

    if(usea_){
      //derivative with respect to a
      for(int p=0;p<nv_;p++){
        DerMatSymm_(k,p)=1;
        kbare++;
      }
      k++;
    }

    if(useb_){
      //derivatives with respect to b
      for(int p=0;p<nh_;p++){
        int ksymm=std::floor(double(k)/double(permsize_));
        DerMatSymm_(ksymm+k,kbare)=1;
        kbare++;
      }
      k+=alpha_;
    }

    //derivatives with respect to W
    for(int i=0;i<nv_;i++){
      for(int j=0;j<nh_;j++){

        int isymm=permtable_[i][j%permsize_];
        int jsymm=std::floor(double(j)/double(permsize_));
        int ksymm=jsymm+alpha_*isymm;

        DerMatSymm_(ksymm+k,kbare)=1;

        kbare++;
      }
    }

    MPI_Comm_rank(MPI_COMM_WORLD, &mynode_);

    if(mynode_==0){
      cout<<"# RBM Initizialized with nvisible = "<<nv_<<" and nhidden = "<<nh_<<endl;
      cout<<"# Symmetry are being used : "<<npar_<<" parameters left, instead of "<<nbarepar_<<endl;
    }
  }

  int Nvisible()const{
    return nv_;
  }

  int Nhidden()const{
    return nh_;
  }

  int Npar()const{
    return npar_;
  }

  void InitRandomPars(int seed,double sigma){

    VectorType par(npar_);

    RbmSpin<T>::RandomGaussian(par,seed,sigma);

    SetParameters(par);
  }


  void InitLookup(const VectorXd & v,LookupType & lt){
    if(lt.VectorSize()==0){
      lt.AddVector(b_.size());
    }
    if(lt.V(0).size()!=b_.size()){
      lt.V(0).resize(b_.size());
    }
    lt.V(0)=(W_.transpose()*v+b_);
  }

  void UpdateLookup(const VectorXd & v,const vector<int>  & tochange,
    const vector<double> & newconf,LookupType & lt){

    if(tochange.size()!=0){

      for(int s=0;s<tochange.size();s++){
        const int sf=tochange[s];
        lt.V(0)+=W_.row(sf)*(newconf[s]-v(sf));
      }

    }
  }

  VectorType BareDerLog(const VectorXd & v){
    VectorType der(nbarepar_);

    int k=0;

    if(usea_){
      for(;k<nv_;k++){
        der(k)=v(k);
      }
    }

    RbmSpin<T>::tanh(W_.transpose()*v+b_,lnthetas_);

    if(useb_){
      for(int p=0;p<nh_;p++){
        der(k)=lnthetas_(p);
        k++;
      }
    }

    for(int i=0;i<nv_;i++){
      for(int j=0;j<nh_;j++){
        der(k)=lnthetas_(j)*v(i);
        k++;
      }
    }
    return der;
  }


  VectorType DerLog(const VectorXd & v){
    return DerMatSymm_*BareDerLog(v);
  }

  VectorType GetParameters(){

    VectorType pars(npar_);

    int k=0;

    if(usea_){
      pars(k)=asymm_;
      k++;
    }

    if(useb_){
      for(int p=0;p<(alpha_);p++){
        pars(k)=bsymm_(p);
        k++;
      }
    }

    for(int i=0;i<nv_;i++){
      for(int j=0;j<alpha_;j++){
        pars(k)=Wsymm_(i,j);
        k++;
      }
    }

    return pars;
  }


  void SetParameters(const VectorType & pars){

    int k=0;

    if(usea_){
      asymm_=pars(k);
      k++;
    }
    else{
      asymm_=0;
    }

    if(useb_){
      for(int p=0;p<alpha_;p++){
        bsymm_(p)=pars(k);
        k++;
      }
    }
    else{
      bsymm_=VectorType::Zero(alpha_);
    }

    for(int i=0;i<nv_;i++){
      for(int j=0;j<alpha_;j++){
        Wsymm_(i,j)=pars(k);
        k++;
      }
    }

    SetBareParameters();
  }

  void SetBareParameters(){
    //Setting the bare values of the RBM parameters
    for(int i=0;i<nv_;i++){
      a_(i)=asymm_;
    }

    for(int j=0;j<nh_;j++){
      int jsymm=std::floor(double(j)/double(permsize_));
      b_(j)=bsymm_(jsymm);
    }

    for(int i=0;i<nv_;i++){
      for(int j=0;j<nh_;j++){
        int jsymm=std::floor(double(j)/double(permsize_));
        W_(i,j)=Wsymm_(permtable_[i][j%permsize_],jsymm);
      }
    }
  }

  //Value of the logarithm of the wave-function
  T LogVal(const VectorXd & v){
    RbmSpin<T>::lncosh(W_.transpose()*v+b_,lnthetas_);

    return (v.dot(a_)+lnthetas_.sum());
  }

  //Value of the logarithm of the wave-function
  //using pre-computed look-up tables for efficiency
  T LogVal(const VectorXd & v,LookupType & lt){
    RbmSpin<T>::lncosh(lt.V(0),lnthetas_);

    return (v.dot(a_)+lnthetas_.sum());
  }

  //Difference between logarithms of values, when one or more visible variables are being flipped
  VectorType LogValDiff(const VectorXd & v,
    const vector<vector<int> >  & tochange,
    const vector<vector<double>> & newconf){


    const int nconn=tochange.size();
    VectorType logvaldiffs=VectorType::Zero(nconn);

    thetas_=(W_.transpose()*v+b_);
    RbmSpin<T>::lncosh(thetas_,lnthetas_);

    T logtsum=lnthetas_.sum();

    for(int k=0;k<nconn;k++){

      if(tochange[k].size()!=0){

        thetasnew_=thetas_;

        for(int s=0;s<tochange[k].size();s++){
          const int sf=tochange[k][s];

          logvaldiffs(k)+=a_(sf)*(newconf[k][s]-v(sf));

          thetasnew_+=W_.row(sf)*(newconf[k][s]-v(sf));
        }

        RbmSpin<T>::lncosh(thetasnew_,lnthetasnew_);
        logvaldiffs(k)+=lnthetasnew_.sum() - logtsum;

      }
    }
    return logvaldiffs;
  }

  //Difference between logarithms of values, when one or more visible variables are being flipped
  //Version using pre-computed look-up tables for efficiency on a small number of spin flips
  T LogValDiff(const VectorXd & v,const vector<int>  & tochange,
    const vector<double> & newconf,const LookupType & lt){

    T logvaldiff=0.;

    if(tochange.size()!=0){

      RbmSpin<T>::lncosh(lt.V(0),lnthetas_);

      thetasnew_=lt.V(0);

      for(int s=0;s<tochange.size();s++){
        const int sf=tochange[s];

        logvaldiff+=a_(sf)*(newconf[s]-v(sf));

        thetasnew_+=W_.row(sf)*(newconf[s]-v(sf));
      }

      RbmSpin<T>::lncosh(thetasnew_,lnthetasnew_);
      logvaldiff+=(lnthetasnew_.sum()-lnthetas_.sum());
    }
    return logvaldiff;
  }


  const Hilbert& GetHilbert()const{
    return hilbert_;
  }

  void to_json(json &j)const{
    j["Machine"]["Name"]="RbmSpinSymm";
    j["Machine"]["Nvisible"]=nv_;
    j["Machine"]["Alpha"]=alpha_;
    j["Machine"]["UseVisibleBias"]=usea_;
    j["Machine"]["UseHiddenBias"]=useb_;
    j["Machine"]["asymm"]=asymm_;
    j["Machine"]["bsymm"]=bsymm_;
    j["Machine"]["Wsymm"]=Wsymm_;
  }

  void from_json(const json & pars){

    if(pars.at("Machine").at("Name")!="RbmSpinSymm"){
      if(mynode_==0){
        cerr<<"# Error while constructing RbmSpinSymm from Json input"<<endl;
      }
      std::abort();
    }

    if(FieldExists(pars["Machine"],"Nvisible")){
      nv_=pars["Machine"]["Nvisible"];
    }
    if(nv_!=hilbert_.Size()){
      if(mynode_==0){
        cerr<<"# Number of visible units is incompatible with given Hilbert space"<<endl;
      }
      std::abort();
    }

    alpha_=FieldVal(pars["Machine"],"Alpha");

    usea_=FieldOrDefaultVal(pars["Machine"],"UseVisibleBias",true);
    useb_=FieldOrDefaultVal(pars["Machine"],"UseHiddenBias",true);

    Init(graph_);

    //Loading parameters, if defined in the input
    if (FieldExists(pars["Machine"],"asymm")){
      asymm_=pars["Machine"]["asymm"].get<T>();
    }
    else{
      asymm_=0;
    }

    if (FieldExists(pars["Machine"],"bsymm")){
      bsymm_=pars["Machine"]["bsymm"];
    }
    else{
      bsymm_.setZero();
    }
    if( FieldExists(pars["Machine"],"Wsymm")){
      Wsymm_=pars["Machine"]["Wsymm"];
    }

    SetBareParameters();
  }

};


}

#endif
