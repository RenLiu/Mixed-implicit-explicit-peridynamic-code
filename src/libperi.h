#include <fstream>
#include <cmath>
#include <vector>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <limits>
#include <sstream>
#include <iomanip>
#include <string>

#include "armadillo"

#include "vtkIntArray.h"
#include "vtkTriangle.h"
#include "vtkCellArray.h"
#include "vtkCellData.h"
#include "vtkDoubleArray.h"
#include "vtkFloatArray.h"
#include "vtkPoints.h"
#include "vtkLine.h"
#include "vtkPolyData.h"
#include "vtkPointData.h"
#include "vtkSmartPointer.h"
#include "vtkXMLPolyDataWriter.h"
#include "vtkPostScriptWriter.h"
#include "vtkImageStencil.h"
#include "vtkImageStencilData.h"
#include "vtkImageStencilSource.h"
#include "vtkPolyDataToImageStencil.h"

#include <time.h>
#include <omp.h>

#define DT_SAVE 100     //decrement of delta_t when we encounter a nan or inf
#define OMEGAKZERO 2    //increment of delta_t when omega_k is zero

std::string FindInFile(std::string Address, std::string Parameter)
{
    std::ifstream InFile(Address.c_str());
    std::string line;
    while(std::getline(InFile,line))
    {
        //std::cout << "read: " << line << std::endl;
        size_t found = line.find_last_of(Parameter.c_str());
        if (found!=std::string::npos)
        {
            line.erase(0,found+1);
            std::cout << line << std::endl;
            return line;
        }
    }
}

double sqr(double arg){
	return arg*arg;
}

class Node
{
	public:			    //public=accesso col punto
	arma::rowvec y; 	    //current position
	arma::rowvec y_k;      //r(L+1,K)
	arma::rowvec y_k1;     //r(L+1,K-1)
	arma::rowvec x;	    //reference position
	arma::rowvec b;	    //external force
	arma::rowvec f;	    //deformation force
	arma::rowvec vel;	    //velocity for verlet scheme
	double m;               //particle mass
	bool bc; 		    //is it a boundary condition?

	Node(arma::rowvec pos, arma::rowvec ext_load, arma::rowvec velocities,double mass, int bcflag);
	~Node();
};

//constructor of Node
Node::Node(arma::rowvec pos, arma::rowvec ext_load, arma::rowvec velocities, double mass, int bcflag)
{
	y = x = pos;
	b = ext_load;
	vel = velocities;
	f.zeros(3);
	y_k.zeros(3);
	y_k1.zeros(3);
	m = mass;
	if(bcflag == 1)
		bc = true;
	else
		bc = false;
}

//destructor of Node
Node::~Node()
{
	//cout << "Node destroyed \n";
}

class Link
{
	public:
	unsigned int node1;  	//indice nel vettore dei nodi
	unsigned int node2;     //ATTENZIONE il primo nodo si chiama nodo 0
	bool undamaged;			//se si rompe, diventa falso
	arma::rowvec xi;				//distanza nella conf. riferimento
	double norm_xi;              //modulo della distanza di riferimento
	arma::rowvec curr_dist;				//distanza nella conf. deformata
	arma::rowvec force;            //forza concorde con eta

	Link(unsigned int i, unsigned int j,
			arma::rowvec& v1, arma::rowvec& v2);
	~Link();
};

//constructor of link
Link::Link(unsigned int i, unsigned int j,
		arma::rowvec& v1, arma::rowvec& v2)
{
	node1 = i;
	node2 = j;
	undamaged = true;

	xi  = v2 - v1;
	curr_dist = xi;
	force.zeros(3);
	norm_xi = arma::norm(xi,2);
	//cout << "link created \n";
}

//destructor of link
Link::~Link()
{
	//cout << "link destroyed \n";
}

bool DuplicationCheck(unsigned int i, unsigned int j, const std::vector< std::vector<unsigned int> > &CompVec)
{
    for (std::vector< std::vector<unsigned int> >::size_type u = 0; u < CompVec.size(); u++)
    {
        if(((i==CompVec[u][0]) && (j==CompVec[u][1])) || ((j==CompVec[u][0]) && (i==CompVec[u][1])))
            return false;
    }

    return true;
}

bool HorizonCheck(unsigned int i, unsigned int j, double delta,const std::vector<Node> &NodesVector) //forse si dovrebbe passare il puntatore
{
    double distance = arma::norm((NodesVector[j].x - NodesVector[i].x),2);
	return(distance <= delta);
}

void VelocitVerlet_1_2(double delta_t, std::vector<Node> &NodesVector)
{
    #pragma omp parallel for
    for(int i=0;i<NodesVector.size();i++)
        {
            if(NodesVector[i].bc == false)
            {
                //velocity verlet step 1 and 2 DIMENSIONAL form
				NodesVector[i].vel += (delta_t/(2*NodesVector[i].m)) * (NodesVector[i].f + NodesVector[i].b);
			}
            NodesVector[i].y += NodesVector[i].vel * delta_t;  //new position
        }
}


void ZeroBelowEpsilonV(arma::rowvec& vec){
			for(int j = 0; j < 3; j++){
				if(fabs(vec[j]) < (std::numeric_limits<double>::epsilon()))
                    vec[j] = 0;
			}
}
void ZeroBelowEpsilon(double& scalar){
        if(fabs(scalar) < (std::numeric_limits<double>::epsilon()) ) scalar = 0;              //si può usare anche long double
}



void VelocityVerlet_3(double delta_t, std::vector<Node> &NodesVector)
{
    #pragma omp parallel for
    for(int i=0;i<NodesVector.size();i++)
    {
        if(NodesVector[i].bc == false)
        {
            NodesVector[i].vel += (delta_t/(2*NodesVector[i].m)) * (NodesVector[i].f + NodesVector[i].b);
        }
    }
}

void EraseBrokenLinks(std::vector<Link> &LinksVector)
{
    unsigned int prev_links = LinksVector.size();
    std::vector<Link>::iterator LinksCleaner;
    LinksCleaner=LinksVector.begin();
    while(LinksCleaner!=LinksVector.end())
    {
        if((*LinksCleaner).undamaged!=true) LinksVector.erase(LinksCleaner);
        else LinksCleaner++;
    }
    if(LinksVector.size() < prev_links) std::cout << LinksVector.size() << " links left after fractures" << std::endl;
}

void NodalForceComputation(std::vector<Node> &NodesVector, std::vector<Link> &LinksVector,double thickness)
{
    #pragma omp parallel for
	for(int i=0;i<NodesVector.size();i++)
    {
        //clear interaction force
		NodesVector[i].f.zeros(3);
	}


    std::vector<Link>::iterator LinksIterator;
	for(LinksIterator=LinksVector.begin();LinksIterator!=LinksVector.end();LinksIterator++)
	{
		unsigned int node1i = LinksIterator->node1;
		unsigned int node2i = LinksIterator->node2;

		Node& node1 = NodesVector[node1i];
		Node& node2 = NodesVector[node2i];

        if(node1.bc == false) node1.f += thickness * LinksIterator->force;                  //controllare le direzioni
        if(node2.bc == false) node2.f -= thickness * LinksIterator->force;                  //controllare le direzioni

	}
}

void LinkForceUpdate(std::vector<Node> &NodesVector, std::vector<Link> &LinksVector, double s_critical, double c, double area, int switch_case)
{
	double s_compression = 0.1;

    #pragma omp parallel for
	for(int i=0;i<LinksVector.size();i++)
	{
		if(switch_case == 0){
			LinksVector[i].curr_dist = NodesVector[LinksVector[i].node2].y - NodesVector[LinksVector[i].node1].y;  //new relative position
		} else {
			LinksVector[i].curr_dist = NodesVector[LinksVector[i].node2].y_k1 - NodesVector[LinksVector[i].node1].y_k1;  //new relative position
		}

		//temporary check
//        for(unsigned int z=0;z<LinksVector.size();z++)
//        {
//        	if(LinksVector[z].eta[0]!=0 || LinksVector[z].eta[1]!=0 || LinksVector[z].eta[2]!=0)
//        		LinksVector[z].eta.print("eta = ");
//        }
		ZeroBelowEpsilonV(LinksVector[i].curr_dist);

        //new force
		double s = ( arma::norm(LinksVector[i].curr_dist,2) - LinksVector[i].norm_xi) / LinksVector[i].norm_xi;
		//Vector3 direction = LinksVector[i].eta / LinksVector[i].eta.norm();
		ZeroBelowEpsilon(s);
		//if(s < (std::numeric_limits<double>::epsilon()) ) s = 0;              //si può usare anche long double
        if(s==0)
        {
			LinksVector[i].force.zeros(3); //this is wrong, but how to fix it?
        }
        else
        {
            LinksVector[i].force = 2*c*s*LinksVector[i].curr_dist *LinksVector[i].norm_xi*area/norm(LinksVector[i].curr_dist,2) ;           //manca la parte del volume, da mettere forse fuori
            //LinksVector[i].force = 2*c/LinksVector[i].norm_xi*s*LinksVector[i].eta*area / LinksVector[i].eta.norm();           //manca la parte del volume, da mettere forse fuori
			ZeroBelowEpsilonV( LinksVector[i].force );
        }

		if(switch_case == 0){
            //fracture
			LinksVector[i].undamaged = ((s < s_critical) && (s > -s_compression));
		}
	}
	return;
}


void WriteGnuplotFile(std::vector<Node> &NodesVector, int counter) //to put in setw
{
    std::string ResultsFileName("results.out."); //output filename creation
    std::stringstream ss;
	ss << std::setfill('0') << std::setw(8) << counter;
    std::string i_str = ss.str(); //retrieve as a string
    ResultsFileName += i_str;  //this string must be converted to char by c_str()
    std::ofstream ResultsFile(ResultsFileName.c_str());
    for(unsigned int i=0;i<NodesVector.size();i++)
    {
		ResultsFile << NodesVector[i].y << std::endl;
    }
    ResultsFile.close();
}




double Omega(std::vector<Node> &NodesVector, int switch_case) //0 = omega_0, 1 = omega_k
{
	double num = 0, den = 0;

	for(unsigned int i=0;i<NodesVector.size();i++)
    {

		arma::rowvec num_vec, den_vec;
		num_vec.zeros(3); den_vec.zeros(3);

		for(unsigned int j=0;j<3;j++)
    	{ //check that distance does not exceed numerical limits
			if(switch_case == 0)
			{
				if( fabs(NodesVector[i].y_k[j]-NodesVector[i].y[j]) > (std::numeric_limits<double>::epsilon()))
    				num_vec[j] = (NodesVector[i].y_k[j]-NodesVector[i].y[j]);
			} else
			{
				if( fabs(NodesVector[i].y_k[j]-NodesVector[i].y_k1[j]) > (std::numeric_limits<double>::epsilon()))
        			num_vec[j] = (NodesVector[i].y_k[j]-NodesVector[i].y_k1[j]);
        		if( fabs(NodesVector[i].y_k[j]-NodesVector[i].y[j]) > (std::numeric_limits<double>::epsilon()))
        			den_vec[j] = (NodesVector[i].y_k[j]-NodesVector[i].y[j]);
			}
    	}
		num += arma::norm(num_vec,2);
		if(switch_case == 0){
			den += arma::norm(NodesVector[i].y,2);
		} else {
			den += arma::norm(den_vec,2);
		}
    }
    return num/den;
}

void Zohdi(std::vector<Node> &Nodes, std::vector<Link> &Links,
             double c, double delta_x, double &delta_t, double TOL, int KD, int p, double thickness,
             unsigned int &zohdi_counter)
{
    double area = delta_x*delta_x;
    double phi = 0;
    bool go_ahead = false;
    while(go_ahead == false)
    {
        //y(L+1,K) calculation  = Dt^2/m*f(k-1) + y(L) + DT*vel(L)
        for(unsigned int i=0;i<Nodes.size();i++)
        {
            Nodes[i].y_k = (sqr(delta_t) / Nodes[i].m) * ( Nodes[i].f + Nodes[i].b ) + Nodes[i].y + delta_t*Nodes[i].vel;
        }
        //starting error computation
        double omega_0, omega_k;
        omega_0 = Omega(Nodes,0);
            //avoid infinite loops
        if(omega_0 == 0 || omega_0 < std::numeric_limits<double>::epsilon() )
        {
            delta_t = delta_t*OMEGAKZERO;
            break;
        }
        else while(omega_0 == std::numeric_limits<double>::infinity() || omega_0 != omega_0)
        {
            std::cout << "Omega_0: Nan or Inf detected" << std::endl;
            delta_t = delta_t/DT_SAVE;
            for(unsigned int i=0;i<Nodes.size();i++)
            {
                Nodes[i].y_k = (sqr(delta_t) / Nodes[i].m) * ( Nodes[i].f + Nodes[i].b ) + Nodes[i].y + delta_t*Nodes[i].vel;
            }
            omega_0 = Omega(Nodes,0);
        }

        //avoid infinite loops: delta_t unmodified if no error
        for(unsigned int internal_count=2;internal_count<=KD;internal_count++)
        {
            for(unsigned int i=0;i<Nodes.size();i++)
            {
                Nodes[i].y_k1 = Nodes[i].y_k;
            }
            //link force update (based on node position update, just done)--without fractureoooooooooooooooooooooooooooooooo
            LinkForceUpdate(Nodes,Links, std::numeric_limits<double>::infinity(),c,area,1);

            //force acting on each node computation
            NodalForceComputation(Nodes,Links,thickness);

            //y(L+1,K) calculation  = Dt^2/m*f(k-1) + y(L) + DT*vel(L)
            for(unsigned int i=0;i<Nodes.size();i++)
            {
                Nodes[i].y_k = std::pow(delta_t,2)/Nodes[i].m*(Nodes[i].f + Nodes[i].b) + Nodes[i].y + delta_t*Nodes[i].vel;
            }
            //error k calculation
            omega_k = Omega(Nodes,1);

            if( omega_k != omega_k || omega_k == std::numeric_limits<double>::infinity() )
            {
                std::cout << "Omega_k: Nan or Inf detected" << std::endl;
                delta_t = delta_t/DT_SAVE;
                LinkForceUpdate(Nodes,Links,std::numeric_limits<double>::infinity(),c,area,0); //preserve actual stable force
                NodalForceComputation(Nodes,Links,thickness);
                go_ahead = false;
                break;
            }
            else if(omega_k < TOL && omega_k > 0 && omega_k  > std::numeric_limits<double>::epsilon() )
            {
                double phi_temp = pow((TOL/omega_0),(double)1/(p*KD))/pow((omega_k/omega_0),(double)1/(p*internal_count));
                if(phi_temp != std::numeric_limits<double>::infinity())
                {
                    phi = phi_temp;
                    delta_t = delta_t*phi;
                }
                go_ahead = true;
                break;
            }
            else if(omega_k == 0 || omega_k < std::numeric_limits<double>::epsilon())
            {
                delta_t = OMEGAKZERO *delta_t;
                go_ahead = true;
                break;
            }
        }

        if(omega_k > TOL && !( omega_k != omega_k || omega_k == std::numeric_limits<double>::infinity() ))  //forse da controllare pure qua
        {
            double phi_temp = pow((TOL/omega_k),(double)1/(p*KD));
            if(phi_temp != std::numeric_limits<double>::infinity())
            {
                phi = phi_temp;
                delta_t = delta_t*phi;
                if(phi == 1)
                {
                    go_ahead = true;
                    break;
                }
            }
            go_ahead = false;
            LinkForceUpdate(Nodes,Links,std::numeric_limits<double>::infinity(),c,area,0); //preserve actual stable force
            NodalForceComputation(Nodes,Links,thickness);
            if(phi == 1)
                go_ahead = true; //prevent blocks for too much precision
        }
//      else if( omega_k != omega_k || omega_k == std::numeric_limits<double>::infinity() )
//      {
//         	delta_t = delta_t/DT_SAVE;
//			LinkForceUpdate(Nodes,Links,s_critical,c,area,0); //preserve actual stable force
//			NodalForceComputation(Nodes,Links,thickness);
//          go_ahead = false;
//      }
        zohdi_counter++;
    }
}


void WriteVTKbyVTK(std::vector<Node> &NodesVector, std::vector<Link> &LinksVector, int counter)
{
    vtkSmartPointer<vtkPoints> points3D = vtkSmartPointer<vtkPoints>::New();
	vtkSmartPointer<vtkCellArray> Vertices = vtkSmartPointer<vtkCellArray>::New();
	vtkSmartPointer<vtkCellArray> lines3D = vtkSmartPointer<vtkCellArray>::New();

	for(unsigned int i=0;i<NodesVector.size();i++)
	{
		vtkIdType pid[1];
		pid[0] = points3D->InsertNextPoint(NodesVector[i].y[0], NodesVector[i].y[1], NodesVector[i].y[2]);
		Vertices->InsertNextCell(1,pid);
	}

	std::vector<Link>::iterator LinksIterator;
	for(LinksIterator=LinksVector.begin();LinksIterator!=LinksVector.end();LinksIterator++)
    {
	vtkIdType pid[1]; //debug
	vtkIdType nd1 = LinksIterator->node1;
	vtkIdType nd2 = LinksIterator->node2;
	pid[0] = lines3D->InsertNextCell(2);
	lines3D->InsertCellPoint(nd1); //devo beccare l'id: l'ordine è lo stesso di std::vector?si
	lines3D->InsertCellPoint(nd2);
    }

	vtkSmartPointer<vtkPolyData> polydata = vtkPolyData::New();
	polydata->SetPoints(points3D);
	polydata->SetVerts(Vertices);
	polydata->SetLines(lines3D);	

	vtkSmartPointer<vtkXMLPolyDataWriter> writer = vtkSmartPointer<vtkXMLPolyDataWriter>::New();
	writer->SetInput(polydata);

	std::string ResultsFileName("periVTK"); //output filename creation
        std::stringstream ss;
        ss << setfill('0') << setw(8) << counter;
        std::string i_str = ss.str(); //retrieve as a string
    
        ResultsFileName += i_str + ".vtp";  //this string must be converted to char by c_str()
	writer->SetFileName(ResultsFileName.c_str());
	writer->Write();

	//write only points avoiding lines: new definition from scratch
	vtkSmartPointer<vtkPolyData> pointpolydata = vtkPolyData::New();
	pointpolydata->SetPoints(points3D);
	pointpolydata->SetVerts(Vertices);
	vtkSmartPointer<vtkXMLPolyDataWriter> pointwriter = vtkSmartPointer<vtkXMLPolyDataWriter>::New();
	pointwriter->SetInput(pointpolydata);
	std::string PointResultsFileName("periPOINT");
	PointResultsFileName += i_str + ".vtp";
	pointwriter->SetFileName(PointResultsFileName.c_str());
	pointwriter->Write();
}

void WriteVTKFile(std::vector<Node> &NodesVector, std::vector<Link> &LinksVector, int counter)
{
    std::string ResultsFileName("peri.vtp."); //output filename creation
    std::stringstream ss;
    ss << setfill('0') << setw(8) << counter;
    std::string i_str = ss.str(); //retrieve as a string
    ResultsFileName += i_str;  //this string must be converted to char by c_str()
    std::ofstream VtkResults(ResultsFileName.c_str());
    VtkResults << "# vtk DataFile Version 3.0" << std::endl;
    VtkResults << "vtk output" << std::endl << "ASCII" << std::endl << "DATASET POLYDATA" << std::endl << std::endl;
    VtkResults << "POINTS " << NodesVector.size() << " double" << std::endl;

    for(unsigned int i=0;i<NodesVector.size();i++)
    {
        VtkResults << NodesVector[i].y[0] << " " << NodesVector[i].y[1] << " " << NodesVector[i].y[2] << "\n";
    }

    VtkResults << std::endl << "LINES " << LinksVector.size() << " " << LinksVector.size()*3 << std::endl;

    for(unsigned int i=0;i<LinksVector.size();i++)
    {
        VtkResults << "2 " << LinksVector[i].node1 << " " << LinksVector[i].node2 << std::endl;
    }

    VtkResults << std::endl << "POINT_DATA " << NodesVector.size() << std::endl;
    VtkResults << "SCALARS element float" << std::endl << "LOOKUP_TABLE default" << std::endl << "1 1 1" << std::endl;
    VtkResults.close();

}

void WriteGnuplot(std::vector<Node> &Nodes, std::vector<Link> &Links)
{
    arma::mat NodesGnuplot = arma::zeros<arma::mat>(Nodes.size(),3);
    for(unsigned int i=0;i<Nodes.size();i++)
    {
        NodesGnuplot.submat(i,0,i,2) = Nodes[i].y;
    }
    NodesGnuplot.save("Nodes.gnuplot",arma::raw_ascii);
    //Links:
    arma::mat LinksGnuplot = arma::zeros<arma::mat>(Links.size(),6);
    for(unsigned int i=0;i<Links.size();i++)
    {
        LinksGnuplot.submat(i,0,i,2) = Nodes[Links[i].node1].y;
        LinksGnuplot.submat(i,3,i,5) = Nodes[Links[i].node2].y;
    }
    LinksGnuplot.save("Links.gnuplot",arma::raw_ascii);
}


void WriteGMSH(std::vector<Node> &Nodes, std::vector<Link> &Links, int counter)
{
    std::string ResultsFileName("peri."); //output filename creation
    std::stringstream ss;
    ss << setfill('0') << setw(8) << counter;
    std::string i_str = ss.str(); //retrieve as a string
    ResultsFileName += i_str + ".geo";  //this string must be converted to char by c_str()    
    std::ofstream GMSHfile(ResultsFileName.c_str());
    //nodes
    for(unsigned int i=0;i<Nodes.size();i++)
    {
        GMSHfile << "Point(" << i+1 << ") = {" << Nodes[i].y[0] << ", " << Nodes[i].y[1] << ", " << Nodes[i].y[2] <<"};" << std::endl;
    }
    //Links:
    for(unsigned int i=0;i<Links.size();i++)
    {
        GMSHfile << "Line(" << (i+1) << ") = {" << (Links[i].node1 +1) << ", "<< (Links[i].node2 +1) << "};" << std::endl;
    }
    GMSHfile.close();
}

void PeriIterations(std::vector<Node> &Nodes, std::vector<Link> &Links, double t, double t_stop,
                    double delta_t, double c, double delta_x, double TOL, double KD, double p, double thickness,
                    double s_critical, double area, double num_steps, unsigned int write_delta)
{
    std::ofstream DTFile("delta_t.txt");

    unsigned int OutputFileCounter=0,counter=1;
    while(t <= t_stop && counter <= num_steps)
    {
        //nodes position update
        VelocitVerlet_1_2(delta_t,Nodes);

        unsigned int zohdi_counter = 0;
        Zohdi(Nodes,Links,c,delta_x,delta_t,TOL,KD,p,thickness,zohdi_counter);

        //link force update (based on node position update, just done)
        //1 = external iteration
        LinkForceUpdate(Nodes,Links,s_critical,c,area,0);

        //force acting on each node computation
        NodalForceComputation(Nodes,Links,thickness);

        //velocity verlet step 3 DIMENSIONAL form
        VelocityVerlet_3(delta_t,Nodes);

        //damping
        for(unsigned int i=0;i<Nodes.size();i++)
        {
			if(Nodes[i].bc == false)
				Nodes[i].vel = 0.9*Nodes[i].vel;
        }

        //broken links elimination
        EraseBrokenLinks(Links);

        //output file production
        if(counter < 10)
        {
        	WriteVTKbyVTK(Nodes,Links,counter);
                WriteGMSH(Nodes,Links,counter);
        }
        else
        {
        	OutputFileCounter++;
        	if(OutputFileCounter==write_delta)
        	{
        		OutputFileCounter = 0;
        		WriteVTKbyVTK(Nodes,Links,counter);
			WriteGMSH(Nodes,Links,counter);
        		std::cout << "                                                                                "
        		<< (((float)counter+1)/((float)num_steps)*100) << "% of entire process\n";
        	}
        }

        t += delta_t;
        counter++;
        DTFile << t << " " << delta_t << " " << zohdi_counter << std::endl;
    }
    DTFile.close();
}


void WriteSaveFiles(std::vector<Node> &Nodes, std::vector<Link> &Links)
{
    arma::mat NodesSave = arma::zeros<arma::mat>(Nodes.size(),16);
    for(unsigned int i=0;i<Nodes.size();i++)
    {
        NodesSave.submat(i,0,i,2) = Nodes[i].x;
        NodesSave.submat(i,3,i,5) = Nodes[i].y;
        NodesSave.submat(i,6,i,8) = Nodes[i].vel;
        NodesSave.submat(i,9,i,11) = Nodes[i].b;
        NodesSave.submat(i,12,i,14) = Nodes[i].f;
        NodesSave(i,15) = Nodes[i].m;
    }
    NodesSave.save("Nodes.save",arma::raw_ascii);
    //Links:
    arma::mat LinksSave = arma::zeros<arma::mat>(Links.size(),8);
    for(unsigned int i=0;i<Links.size();i++)
    {
        LinksSave(i,0) = Links[i].node1;
        LinksSave(i,1) = Links[i].node2;
        LinksSave.submat(i,2,i,4) = Links[i].xi;
        LinksSave.submat(i,5,i,7) = Links[i].curr_dist;
    }
    LinksSave.save("Links.save",arma::raw_ascii);
}
