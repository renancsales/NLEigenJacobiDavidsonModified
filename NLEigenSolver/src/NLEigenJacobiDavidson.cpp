#include "nlpch.h"
#include "NLEigenJacobiDavidson.h"

#include <Eigen/IterativeLinearSolvers>

NLEigenJacobiDavidson::NLEigenJacobiDavidson(const std::string& filepath)
	: m_Dimensions(0), m_NumberOfMassMtx(1), m_NumberOfEigenValues(0),
	m_MaxIter(20), m_TOL(1e-12), m_FilePath(filepath)
{
	// Initialize the log system
	Log::Init();
}

NLEigenJacobiDavidson::~NLEigenJacobiDavidson()
{

}

void NLEigenJacobiDavidson::execute()
{
	//Reading data
	LOG_INFO("Reading filedata...\n");
	Eigen::MatrixXd K0;
	std::vector<Eigen::MatrixXd> MM;
	readFileAndGetStiffMassMatrices(K0, MM);

	LOG_INFO("Initialize the matrices...\n");
	//Initialize the matrices and set the matrix as zero
	Eigen::VectorXd Omega(m_NumberOfEigenValues);
	Eigen::VectorXd rk,dUk;
	Eigen::MatrixXd Phi(m_Dimensions, m_NumberOfEigenValues);
	Eigen::MatrixXd B_r(m_Dimensions, m_NumberOfEigenValues);
	Eigen::MatrixXd Keff(m_Dimensions, m_Dimensions);
	Eigen::MatrixXd Kn(m_Dimensions, m_Dimensions);
	Eigen::MatrixXd Mn(m_Dimensions, m_Dimensions);
	Eigen::MatrixXd Mlrls(m_Dimensions, m_Dimensions);

	//Set as zero
	Omega.setZero();  Phi.setZero(); B_r.setZero();
	Keff.setZero(); Kn.setZero(); Mn.setZero(); Mlrls.setZero();

	double conv, PtMP, PtKP, theta;
	int iterK;

	LOG_INFO("Processing...");
	// Loop in each eigenvalue
	for (int ie = 0; ie < m_NumberOfEigenValues; ie++)
	{
		// Set the converge
		conv = 1.0;
		iterK = 0;

		LOG_INFO("Eigenvalue #{0}:", ie);

		if (ie > 0)
		{
			Omega(ie) = Omega(ie - 1);
		}

		while (abs(conv) > m_TOL)
		{
			// Orthogonalized phi_r with respect to phi_s
			for (int is = 0; is < ie; is++)
			{
				// Get the generalized freq-dependent mass matrix
				getGeneralizedFreqDependentMassMtx(MM, Mlrls, Omega(ie), Omega(is));
				B_r.col(ie) = Mlrls * Phi.col(is);

				if (is > 0)
				{
					for (int el = 0; el < is ; el++)
					{
						//Project b_s = b_s - b_el*(b_el.t()*b_s)
						B_r.col(is) += -B_r.col(el) * (B_r.col(el).transpose() * B_r.col(is));
					}
				}

				// Normalize
				B_r.col(is) = 1.0 / (sqrt(B_r.col(is).transpose() * B_r.col(is))) * B_r.col(is);
			}

			// Orthogonalize phi_e with respect to the preceding eigenvector phi
			if (ie > 0)
			{
				for (int is = 0; is < ie; is++)
				{
					Phi.col(ie) += -B_r.col(is) * (B_r.col(is).transpose() * Phi.col(ie));
				}
			}

			// Evaluate the effective stiffness matrix
			getEffectiveStiffMtx(K0, MM, Keff, Omega(ie));
			
			// Evaluate the residual error
			rk = -Keff * Phi.col(ie);

			// Project the effective stiffness matrix
			projectEffectiveStiffMatrix(Keff, B_r, ie);

			// solve dUk
			iterativeLinearSolver(Keff, rk, dUk);

			//Project dUk
			for (int is = 0; is < ie + 1; is++)
			{
				dUk += -B_r.col(is) * (B_r.col(is).transpose() * dUk);
			}

			// Update solution
			Phi.col(ie) += dUk;

			// Evaluate the Rayleigh quotient
			getFreqDependentStiffMtx(K0, MM, Kn, Omega(ie));
			getFreqDependentMassMtx(MM, Mn, Omega(ie));

			PtMP = Phi.col(ie).transpose() * Mn * Phi.col(ie);
			PtKP = Phi.col(ie).transpose() * Kn * Phi.col(ie);
			theta = PtKP / PtMP;

			LOG_ASSERT(PtMP < 0, "Error: Negative mass matrix!!!");

			// Normalize the improved eigenvector
			Phi.col(ie) = (1.0 / sqrt(PtMP)) * Phi.col(ie);

			// Evaluate the convergence
			conv = abs(theta - Omega(ie)) / theta;

			LOG_INFO("iter: {0}    rel.error: {1}", iterK, conv);

			//Update the new eigenvalue
			Omega(ie) = theta;

			// Check the max. number of iterations
			iterK++;

			if (iterK > m_MaxIter)
			{
				LOG_ERROR("Error: It has reached the max. number of iterations!!");
				break;
			}

		}
	}

}

void NLEigenJacobiDavidson::readFileAndGetStiffMassMatrices(Eigen::MatrixXd& K0, std::vector<Eigen::MatrixXd>& MM)
{
	//Open file to read
	std::fstream fid;
	fid.open(m_FilePath, std::ios::in);

	//Check error
	if (fid.fail())
	{
		LOG_ASSERT(fid.fail(),"ERROR: Error in opening the file!");
		exit(1);
	}

	if (fid.is_open())
	{
		std::string line;
		std::getline(fid, line);
		// Read #dof, #mass matrices, #eigenvalues
		fid >> m_Dimensions >> m_NumberOfMassMtx >> m_NumberOfEigenValues;

		// Set the matrices
		K0 = Eigen::MatrixXd(m_Dimensions, m_Dimensions);
		Eigen::MatrixXd Mtemp(m_Dimensions, m_Dimensions);
		K0.setZero(); Mtemp.setZero();
		MM.reserve(m_NumberOfMassMtx);

		// Read the stiffness matrix K0
		for (int ii = 0; ii < m_Dimensions; ii++)
		{
			for (int jj = 0; jj < m_Dimensions; jj++)
			{
				fid >> K0(ii, jj);
			}
		}

		LOG_INFO("Matrix K0 = \n {0}", K0);
		//Read mass matrices
		for (int im = 0; im < m_NumberOfMassMtx; im++)
		{
			for (int ii = 0; ii < m_Dimensions; ii++)
			{
				for (int jj = 0; jj < m_Dimensions; jj++)
				{
					fid >> Mtemp(ii, jj);
				}
			}

			MM.emplace_back(Mtemp);
		}
	}

	//Close file
	fid.close();	
}

void NLEigenJacobiDavidson::printResults(Eigen::VectorXd& Omega, Eigen::MatrixXd& Phi) const
{
	// Save the eigenproblem results
	std::string directory;
	const size_t last_slash_idx = m_FilePath.rfind('/');
	if (std::string::npos != last_slash_idx)
	{
		directory = m_FilePath.substr(0, last_slash_idx);
	}

	std::string resultFile1, resultFile2;
	resultFile1 = directory + "/Phi.dat";
	resultFile2 = directory + "/Omega.dat";

	std::ofstream out1, out2;
	out1.open(resultFile1);
	out2.open(resultFile2);

	if (!out1 || !out2)
	{
		LOG_ASSERT(false, "ERROR: Error in opening the file!");
		exit(1);
	}

	//Save Phi
	out1 << m_Dimensions <<  " " << m_NumberOfEigenValues << std::endl;
	out1 << std::setprecision(12) << std::scientific << Phi;
	out1.close();

	//Save Omega
	out2 << m_NumberOfEigenValues << std::endl;
	out2 << std::setprecision(12) << std::scientific << Omega;
	out2.close();
}

void NLEigenJacobiDavidson::getFreqDependentStiffMtx(const Eigen::MatrixXd& K0, const std::vector<Eigen::MatrixXd>& MM, Eigen::MatrixXd& Kn, double omega)
{
	//Initialize
	Kn = K0;
    
	for (int jj = 1; jj < m_NumberOfMassMtx; jj++)
	{

		Kn += (jj)*pow(omega, jj + 1.0) * MM[jj];
	}
}

void NLEigenJacobiDavidson::getFreqDependentMassMtx(const std::vector<Eigen::MatrixXd>& MM, Eigen::MatrixXd& Mn, double omega)
{
	//Initialize
	Mn = MM[0];

	for (int jj = 1; jj < m_NumberOfMassMtx; jj++)
	{

		Mn += (jj + 1.0) * pow(omega, jj) * MM[jj];
	}
	
}

void NLEigenJacobiDavidson::getGeneralizedFreqDependentMassMtx(const std::vector<Eigen::MatrixXd>& MM, Eigen::MatrixXd& Mlrls, double lr, double ls)
{
	//Initialize
	Mlrls.setZero();

	for (int jj = 0; jj < m_NumberOfMassMtx; jj++)
	{
		for (int kk = 0; kk < jj + 1; kk++)
		{
			Mlrls += pow(lr, kk) * pow(ls, jj - kk) * MM[jj];
		}
	}
}

void NLEigenJacobiDavidson::getEffectiveStiffMtx(const Eigen::MatrixXd& K0, const std::vector<Eigen::MatrixXd>& MM, Eigen::MatrixXd& Keff, double omega)
{
	// Initialize
	Keff = K0;

	for (int jj = 0; jj < m_NumberOfMassMtx; jj++)
	{
		Keff -= pow(omega, jj + 1.0) * MM[jj];
	}
}

void NLEigenJacobiDavidson::projectEffectiveStiffMatrix(Eigen::MatrixXd& Keff, Eigen::MatrixXd& B_s, int indexEig)
{
	// Project the effective stiffness matrix onto the subspace
	// orthogonal to all preceding eigenvectors and add the orthogonal
	// projector to make it nonsingular
	for (int ii = 0; ii < indexEig; ii++)
	{
		Keff += (B_s.col(ii) - Keff * B_s.col(ii)) * (B_s.col(ii).transpose());
	}
}

bool NLEigenJacobiDavidson::iterativeLinearSolver(Eigen::MatrixXd& A, Eigen::VectorXd& b, Eigen::VectorXd& x)
{
	// Set the iterative linear solver (Conjugate Gradients)
	// The number of max. of iter
	Eigen::ConjugateGradient<Eigen::MatrixXd, Eigen::Lower | Eigen::Upper> linsolver;
	linsolver.setTolerance(1e-12);
	linsolver.compute(A);

	//Solve
	x = linsolver.solve(b);
	
	//LOG_INFO("#Iteration: {0}   Estimated error: {1}", linsolver.iterations(), linsolver.error());

	bool status = true;
	
	if (linsolver.error() < 1e-12)
	{
		status = false;
		LOG_ASSERT(status,"The iterative linear solver has reach the max. iterations with error below of the tolerance!")
	}
		
	return status;
}

