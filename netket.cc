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
#include <fstream>
#include "netket.hh"

using namespace std;
using namespace netket;

int main(int argc,char * argv[]){
  MPI_Init(&argc,&argv);

  if(argc!=2){
    cerr<<"Insert name of input Json file"<<endl;
    std::abort();
  }

  json pars;

  std::ifstream filein(argv[1]);
  if (filein.is_open()){
    filein >> pars;
  }
  else{
    std::cerr<<"Cannot open input file: "<<argv[1]<<endl;
    std::abort();
  }

  Graph graph(pars);

  Hamiltonian<Graph> hamiltonian(graph,pars);

  using Psi=Machine<complex<double>>;
  Psi machine(graph,hamiltonian,pars);

  Sampler<Psi> sampler(graph,hamiltonian,machine,pars);

  Stepper stepper(pars);
  Learning<Hamiltonian<Graph>,Psi,Sampler<Psi>,Stepper> learning(hamiltonian,sampler,stepper,pars);

  MPI_Barrier(MPI_COMM_WORLD);
  MPI_Finalize();
}
