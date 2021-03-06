#include "libperi.h"
#include <cstdlib>
#include <omp.h>

using namespace std;

int main(int argc, char* argv[])
{
    double TOL = atof(argv[4]);//atof(FindInFile("data.in","TOL").c_str());
    double KD = atof(argv[5]);//atof(FindInFile("data.in","KD").c_str());
    double p = atof(argv[6]);//atof(FindInFile("data.in","p").c_str());

    double delta_x = atof(argv[1]);//atof(FindInFile("data.in","delta_x").c_str());     //scendendo sotto 0.025 si pianta tutto
    double Delta = 3.1*delta_x; //horizon = 3*spacing between nodes
    double delta_t = atof(argv[2]); //passo temporale
    double t = 0; //current time
    double t_stop = 300000000;
    double rho = 8000;//atof(FindInFile("data.in","rho").c_str());
    unsigned int num_steps = atof(argv[3]);
    unsigned int write_delta = atof(argv[9]);
    double E = atof(argv[7]);//atof(FindInFile("data.in","E").c_str());

	double thickness = delta_x;
	double area = pow(delta_x,2);
    double m = rho*area*thickness;
    //double s_critical = std::numeric_limits<double>::infinity();
    double s_critical = atof(argv[8]);

	//CreateInputFile("input.per",delta_x,1);
	//CreateInFile("input.per",delta_x,m);

	//creates nodes objects from coordinates in the file
	vector<Node> Nodes;
	vector<Link> Links;
	double c = (12*E)/(3,14159265*(3*delta_x*3*delta_x*3*delta_x*3*delta_x));

    arma::mat in_matrix;
    in_matrix.load("input.per",arma::raw_ascii);
    arma::mat positions = in_matrix.submat(0,0,(in_matrix.n_rows-1),2);
    arma::mat ext_loads = in_matrix.submat(0,3,(in_matrix.n_rows-1),5);
    arma::mat velocities = in_matrix.submat(0,6,(in_matrix.n_rows-1),8);
    arma::colvec mass = in_matrix.submat(0,9,(in_matrix.n_rows-1),9);
    arma::colvec bcflag = in_matrix.submat(0,10,(in_matrix.n_rows-1),10);

    for(int i=0; i < in_matrix.n_rows; i++)
    {
    	Node temp(arma::rowvec(positions.row(i)), arma::rowvec(ext_loads.row(i)), arma::rowvec(velocities.row(i)), mass(i),bcflag(i));
    	Nodes.push_back(temp);
    }

	//links creation
	vector< vector<unsigned int> > CompareVector;
	for(unsigned int i=0;i<Nodes.size();i++)
	{
		for(unsigned int j=0;j<Nodes.size();j++)
		{
			if( (i != j) && (HorizonCheck(i,j,Delta,Nodes)==true) && (DuplicationCheck(i,j,CompareVector)==true))
			{
			    Link temp(i, j, Nodes[i].x, Nodes[j].x);
                Links.push_back(temp);
                vector<unsigned int> tempvec;
                tempvec.push_back(i);
                tempvec.push_back(j);
                CompareVector.push_back(tempvec);
                tempvec.clear();
			}
		}
	}

	//fracture cleaning of links
	cout << Links.size() << " links\n";
    std::vector<Link>::iterator LinksIterator;
    LinksIterator=Links.begin();

    std::cout << Nodes.size() << " nodes\n";
    std::cout << Links.size() << " links\n";
    std::cout << "c = " << c << std::endl;
    std::cout << "Dt = " << delta_t << " Dx = " << delta_x << std::endl;
    std::cout << "TOL = " << TOL << " KD = " << KD << " p = " << p << std::endl;
    std::cout << "  Number of processors available = " << omp_get_num_procs ( ) << std::endl;
    std::cout << "  Number of threads =              " << omp_get_max_threads ( ) << std::endl;

    PeriIterations(Nodes,Links,t,t_stop, delta_t, c, delta_x, TOL, KD, p, thickness, s_critical, area, num_steps, write_delta);

    //resume file for nodes and links
    //Nodes: x,y,vel,b,f,m
    //WriteSaveFiles(Nodes, Links);

	return 0;
}
