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

#ifndef NETKET_SR_HH
#define NETKET_SR_HH

#include <iostream>
#include <iomanip>
#include <Eigen/Dense>
#include <Eigen/IterativeLinearSolvers>
#include <random>
#include <complex>
#include <fstream>
#include <string>
#include <vector>
#include <mpi.h>

namespace netket{

using namespace std;
using namespace Eigen;

//Stochastic reconfiguration optimizer
//both direct and sparse version available
template<class Ham,class Psi,class Samp,class Opt> class Sr : public AbstractLearning<Ham, Psi, Samp, Opt>{

  typedef Matrix<typename Psi::StateType, Dynamic, 1 > VectorT;
  typedef Matrix<typename Psi::StateType, Dynamic, Dynamic > MatrixT;

  Ham & ham_;
  Samp & sampler_;
  Psi & psi_;

  vector<vector<int>> connectors_;
  vector<vector<double>> newconfs_;
  vector<std::complex<double> > mel_;

  VectorXcd elocs_;
  MatrixT Ok_;
  VectorT Okmean_;

  MatrixXd vsamp_;

  VectorXcd grad_;
  VectorXcd gradprev_;

  complex<double> elocmean_;
  double elocvar_;
  int npar_;

  int Iter0_;

  double sr_diag_shift_;
  bool sr_rescale_shift_;
  bool use_iterative_;

  int totalnodes_;
  int mynode_;

  ofstream filelog_;
  string filewfname_;
  double freqbackup_;

  Opt & opt_;

  Observables obs_;
  ObsManager obsmanager_;
  json outputjson_;

  bool dosr_;

public:

  Sr(Ham & ham,Samp & sampler,Opt & opt):
  ham_(ham),sampler_(sampler),psi_(sampler.Psi()),opt_(opt){

    Init();
  }

  //JSON constructor
  Sr(Ham & ham, Samp & sampler, Opt & opt,const json & pars):
  ham_(ham),sampler_(sampler),psi_(sampler.Psi()),opt_(opt),
  obs_(ham.GetHilbert(),pars){

    Init();

    int nsamples=FieldVal(pars["Learning"],"Nsamples");
    int niter_opt=FieldVal(pars["Learning"],"NiterOpt");

    std::string file_base=FieldVal(pars["Learning"],"OutputFile");
    double freqbackup=FieldOrDefaultVal(pars["Learning"],"SaveEvery",100.);
    SetOutName(file_base,freqbackup);

    if(pars["Learning"]["Method"]=="Gd"){
      dosr_=false;
    }
    else{
      double diagshift=FieldOrDefaultVal(pars["Learning"],"DiagShift",0.01);
      bool rescale_shift=FieldOrDefaultVal(pars["Learning"],"RescaleShift",false);
      bool use_iterative=FieldOrDefaultVal(pars["Learning"],"UseIterative",false);

      setSrParameters(diagshift,rescale_shift,use_iterative);
    }

    if(mynode_==0){
      if(dosr_){
        cout<<"# Using the Stochastic reconfiguration method"<<endl;
        if(use_iterative_){
          cout<<"# With iterative solver"<<endl;
        }
      }
      else{
        cout<<"# Using a gradient-descent based method"<<endl;
      }
    }

    Run(nsamples,niter_opt);
  }


  void Init(){
    npar_=psi_.Npar();

    opt_.Init(psi_.GetParameters());

    grad_.resize(npar_);
    Okmean_.resize(npar_);

    Iter0_=0;

    freqbackup_=0;

    setSrParameters();

    obsmanager_.AddObservable("Energy",double());
    obsmanager_.AddObservable("EnergyVariance",double());

    for(int i=0;i<obs_.Size();i++){
      obsmanager_.AddObservable(obs_(i).Name(),double());
    }

    MPI_Comm_size(MPI_COMM_WORLD, &totalnodes_);
    MPI_Comm_rank(MPI_COMM_WORLD, &mynode_);

    if(mynode_==0){
      cout<<"# Learning running on "<<totalnodes_<<" processes"<<endl;
    }
    MPI_Barrier(MPI_COMM_WORLD);
  }

  void Sample(double nsweeps){
    sampler_.Reset();

    int sweepnode=int(std::ceil(double(nsweeps)/double(totalnodes_)));

    vsamp_.resize(sweepnode,psi_.Nvisible());

    for(int i=0;i<sweepnode;i++){
      sampler_.Sweep();
      vsamp_.row(i)=sampler_.Visible();
    }
  }

  //Sets the name of the files on which the logs and the wave-function parameters are saved
  //the wave-function is saved every freq steps
  void SetOutName(string filebase, double freq=50){
    filelog_.open(filebase+string(".log"));
    freqbackup_=freq;

    filewfname_=filebase+string(".wf");
  }

  void Gradient(){

    obsmanager_.Reset("Energy");
    obsmanager_.Reset("EnergyVariance");

    for(int i=0;i<obs_.Size();i++){
      obsmanager_.Reset(obs_(i).Name());
    }

    const int nsamp=vsamp_.rows();
    elocs_.resize(nsamp);
    Ok_.resize(nsamp,psi_.Npar());

    for(int i=0;i<nsamp;i++){
      elocs_(i)=Eloc(vsamp_.row(i));
      Ok_.row(i)=psi_.DerLog(vsamp_.row(i));
      obsmanager_.Push("Energy",elocs_(i).real());

      for(int k=0;k<obs_.Size();k++){
        obsmanager_.Push(obs_(k).Name(),ObSamp(obs_(k),vsamp_.row(i)));
      }
    }

    elocmean_=elocs_.mean();
    SumOnNodes(elocmean_);
    elocmean_/=double(totalnodes_);

    Okmean_=Ok_.colwise().mean();
    SumOnNodes(Okmean_);
    Okmean_/=double(totalnodes_);

    Ok_=Ok_.rowwise()-Okmean_.transpose();

    elocs_-=elocmean_*VectorXd::Ones(nsamp);

    for(int i=0;i<nsamp;i++){
      obsmanager_.Push("EnergyVariance",std::norm(elocs_(i)));
    }

    grad_=2.*(Ok_.adjoint()*elocs_);

    //Summing the gradient over the nodes
    SumOnNodes(grad_);
    grad_/=double(totalnodes_*nsamp);

  }


  std::complex<double> Eloc(const VectorXd & v){

    ham_.FindConn(v,mel_,connectors_,newconfs_);

    assert(connectors_.size()==mel_.size());

    auto logvaldiffs=(psi_.LogValDiff(v,connectors_,newconfs_));

    assert(mel_.size()==logvaldiffs.size());

    std::complex<double> eloc=0;

    for(int i=0;i<logvaldiffs.size();i++){
      eloc+=mel_[i]*std::exp(logvaldiffs(i));
    }

    return eloc;
  }

  double ObSamp(Observable & ob,const VectorXd & v){
    ob.FindConn(v,mel_,connectors_,newconfs_);

    assert(connectors_.size()==mel_.size());

    auto logvaldiffs=(psi_.LogValDiff(v,connectors_,newconfs_));

    assert(mel_.size()==logvaldiffs.size());

    std::complex<double> obval=0;

    for(int i=0;i<logvaldiffs.size();i++){
      obval+=mel_[i]*std::exp(logvaldiffs(i));
    }

    return obval.real();
  }


  double ElocMean(){
    return elocmean_.real();
  }

  double Elocvar(){
    return elocvar_;
  }

  void Run(double nsweeps,double niter){
    opt_.Reset();

    for(double i=0;i<niter;i++){
      Sample(nsweeps);

      Gradient();

      UpdateParameters();

      PrintOutput(i);
    }
    Iter0_+=niter;
  }


  void UpdateParameters(){

    auto pars=psi_.GetParameters();

    if(dosr_){

      const int nsamp=vsamp_.rows();

      VectorXcd b=Ok_.adjoint()*elocs_;
      SumOnNodes(b);
      b/=double(nsamp*totalnodes_);

      if(!use_iterative_){

        //Explicit construction of the S matrix
        MatrixXcd S=Ok_.adjoint()*Ok_;
        SumOnNodes(S);
        S/=double(nsamp*totalnodes_);

        //Adding diagonal shift
        S+=MatrixXd::Identity(pars.size(),pars.size())*sr_diag_shift_;

        FullPivHouseholderQR<MatrixXcd> qr(S.rows(), S.cols());
        qr.setThreshold(1.0e-6);
        qr.compute(S);
        const VectorXcd deltaP=qr.solve(b);
        // VectorXcd deltaP=S.jacobiSvd(ComputeThinU | ComputeThinV).solve(b);

        assert(deltaP.size()==grad_.size());
        grad_=deltaP;

        if(sr_rescale_shift_){
          complex<double> nor=(deltaP.dot(S*deltaP));
          grad_/=std::sqrt(nor.real());
        }

      }
      else{
        Eigen::ConjugateGradient<MatrixReplacement, Eigen::Lower|Eigen::Upper, Eigen::IdentityPreconditioner> it_solver;
        // Eigen::GMRES<MatrixReplacement, Eigen::IdentityPreconditioner> it_solver;
        it_solver.setTolerance(1.0e-3);
        MatrixReplacement S;
        S.attachMatrix(Ok_);
        S.setShift(sr_diag_shift_);
        S.setScale(1./double(nsamp*totalnodes_));

        it_solver.compute(S);
        auto deltaP = it_solver.solve(b);

        grad_=deltaP;
        if(sr_rescale_shift_){
          auto nor=deltaP.dot(S*deltaP);
          grad_/=std::sqrt(nor.real());
        }

        // if(mynode_==0){
        //   cerr<<it_solver.iterations()<<"  "<<it_solver.error()<<endl;
        // }
        MPI_Barrier(MPI_COMM_WORLD);
      }
    }

    opt_.Update(grad_,pars);

    SendToAll(pars);

    psi_.SetParameters(pars);
    MPI_Barrier(MPI_COMM_WORLD);
  }

  void PrintOutput(double i){
    auto Acceptance=sampler_.Acceptance();

    auto jiter=json(obsmanager_);
    jiter["Iteration"]=i+Iter0_;
    outputjson_["Output"].push_back(jiter);

    if(mynode_==0){
      if (jiter["Iteration"]!=0){
        long pos = filelog_.tellp();
        filelog_.seekp(pos - 3);
        filelog_.write(",  ",3);
        filelog_<<jiter<< "]}"<<endl;
      }
      else {
        filelog_<<outputjson_ <<endl;
      }
    }

    if(mynode_==0 && freqbackup_>0 &&  std::fmod(i,freqbackup_)<0.5){
      psi_.Save(filewfname_);
    }

    MPI_Barrier(MPI_COMM_WORLD);
  }


  void setSrParameters(double diagshift=0.01,bool rescale_shift=false,bool use_iterative=false){
    sr_diag_shift_=diagshift;
    sr_rescale_shift_=rescale_shift;
    use_iterative_=use_iterative;
    dosr_=true;
  }


  //Debug function to check that the logarithm of the derivative is
  //computed correctly
  void CheckDerLog(double eps=1.0e-4){

    std::cout<<"# Debugging Derivatives of Wave-Function Logarithm"<<std::endl;
    std::flush(std::cout);

    sampler_.Reset(true);

    auto ders=psi_.DerLog(sampler_.Visible());

    auto pars=psi_.GetParameters();

    for(int i=0;i<npar_;i++){
      pars(i)+=eps;
      psi_.SetParameters(pars);
      typename Psi::StateType valp=psi_.LogVal(sampler_.Visible());

      pars(i)-=2*eps;
      psi_.SetParameters(pars);
      typename Psi::StateType valm=psi_.LogVal(sampler_.Visible());

      pars(i)+=eps;

      typename Psi::StateType numder=(-valm+valp)/(eps*2);

      if(std::abs(numder-ders(i))>eps*eps){
        cerr<<" Possible error on parameter "<<i<<". Expected: "<<ders(i)<<" Found: "<<numder<<endl;
      }
    }
    std::cout<<"# Test completed"<<std::endl;
    std::flush(std::cout);
  }

  inline double real_part(double val)const{
    return val;
  }
  inline double real_part(const std::complex<double> & val)const{
    return val.real();
  }
};


}

#endif
