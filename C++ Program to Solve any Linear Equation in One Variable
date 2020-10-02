#if !defined(MATRIX_H)
#define MATRIX_H
#include <stdio.h>
#include <iostream>
#include <tchar.h>
#include <math.h>
#include <stdlib.h>
 
class CMatrix
{
    private:
        int m_rows;
        int m_cols;
        char m_name[128];
        CMatrix();
    public:
        double **m_pData;
        CMatrix(const char *name, int rows, int cols) :
            m_rows(rows), m_cols(cols)
        {
            strcpy(m_name, name);
            m_pData = new double*[m_rows];
            for (int i = 0; i < m_rows; i++)
                m_pData[i] = new double[m_cols];
            for (int i = 0; i < m_rows; i++)
            {
                for (int j = 0; j < m_cols; j++)
                {
                    m_pData[i][j] = 0.0;
                }
            }
        }
        CMatrix(const CMatrix &other)
        {
            strcpy(m_name, other.m_name);
            m_rows = other.m_rows;
            m_cols = other.m_cols;
            m_pData = new double*[m_rows];
            for (int i = 0; i < m_rows; i++)
                m_pData[i] = new double[m_cols];
            for (int i = 0; i < m_rows; i++)
            {
                for (int j = 0; j < m_cols; j++)
                {
                    m_pData[i][j] = other.m_pData[i][j];
                }
            }
        }
        ~CMatrix()
        {
            for (int i = 0; i < m_rows; i++)
                delete[] m_pData[i];
            delete[] m_pData;
            m_rows = m_cols = 0;
        }
        void SetName(const char *name)
        {
            strcpy(m_name, name);
        }
        const char* GetName() const
        {
            return m_name;
        }
        void GetInput()
        {
            std::cin >> *this;
        }
        void FillSimulatedInput()
        {
            static int factor1 = 1, factor2 = 2;
            std::cout << "\n\nEnter Input For Matrix : " << m_name << " Rows: "
                    << m_rows << " Cols: " << m_cols << "\n";
            for (int i = 0; i < m_rows; i++)
            {
                for (int j = 0; j < m_cols; j++)
                {
                    std::cout << "Input For Row: " << i + 1 << " Col: " << j
                            + 1 << " = ";
                    int data = ((i + 1) * factor1) + (j + 1) * factor2;
                    m_pData[i][j] = data / 10.2;
                    std::cout << m_pData[i][j] << "\n";
                    factor1 += (rand() % 4);
                    factor2 += (rand() % 3);
                }
                std::cout << "\n";
            }
            std::cout << "\n";
        }
        double Determinant()
        {
            double det = 0;
            double **pd = m_pData;
            switch (m_rows)
            {
                case 2:
                {
                    det = pd[0][0] * pd[1][1] - pd[0][1] * pd[1][0];
                    return det;
                }
                    break;
                case 3:
                {
                    /***
                     a b c
                     d e f
                     g h i
 
                     a b c a b c
                     d e f d e f
                     g h i g h i
 
 
                     // det (A) = aei + bfg + cdh - afh - bdi - ceg.
                     ***/
                    double a = pd[0][0];
                    double b = pd[0][1];
                    double c = pd[0][2];
                    double d = pd[1][0];
                    double e = pd[1][1];
                    double f = pd[1][2];
                    double g = pd[2][0];
                    double h = pd[2][1];
                    double i = pd[2][2];
                    double det = (a * e * i + b * f * g + c * d * h); // - a*f*h - b*d*i - c*e*g);
                    det = det - a * f * h;
                    det = det - b * d * i;
                    det = det - c * e * g;
                    //std::cout << *this;
                    //std::cout << "deter: " << det << " \n";
 
                    return det;
                }
                    break;
                case 4:
                {
                    CMatrix *temp[4];
                    for (int i = 0; i < 4; i++)
                        temp[i] = new CMatrix("", 3, 3);
                    for (int k = 0; k < 4; k++)
                    {
                        for (int i = 1; i < 4; i++)
                        {
                            int j1 = 0;
                            for (int j = 0; j < 4; j++)
                            {
                                if (k == j)
                                    continue;
                                temp[k]->m_pData[i - 1][j1++]
                                        = this->m_pData[i][j];
                            }
                        }
                    }
                    double det = this->m_pData[0][0] * temp[0]->Determinant()
                            - this->m_pData[0][1] * temp[1]->Determinant()
                            + this->m_pData[0][2] * temp[2]->Determinant()
                            - this->m_pData[0][3] * temp[3]->Determinant();
                    return det;
                }
                    break;
                case 5:
                {
                    CMatrix *temp[5];
                    for (int i = 0; i < 5; i++)
                        temp[i] = new CMatrix("", 4, 4);
                    for (int k = 0; k < 5; k++)
                    {
                        for (int i = 1; i < 5; i++)
                        {
                            int j1 = 0;
                            for (int j = 0; j < 5; j++)
                            {
                                if (k == j)
                                    continue;
                                temp[k]->m_pData[i - 1][j1++]
                                        = this->m_pData[i][j];
                            }
                        }
                    }
                    double det = this->m_pData[0][0] * temp[0]->Determinant()
                            - this->m_pData[0][1] * temp[1]->Determinant()
                            + this->m_pData[0][2] * temp[2]->Determinant()
                            - this->m_pData[0][3] * temp[3]->Determinant()
                            + this->m_pData[0][4] * temp[4]->Determinant();
                    return det;
                }
                case 6:
                case 7:
                case 8:
                case 9:
                case 10:
                case 11:
                case 12:
                default:
                {
                    int DIM = m_rows;
                    CMatrix **temp = new CMatrix*[DIM];
                    for (int i = 0; i < DIM; i++)
                        temp[i] = new CMatrix("", DIM - 1, DIM - 1);
                    for (int k = 0; k < DIM; k++)
                    {
                        for (int i = 1; i < DIM; i++)
                        {
                            int j1 = 0;
                            for (int j = 0; j < DIM; j++)
                            {
                                if (k == j)
                                    continue;
                                temp[k]->m_pData[i - 1][j1++]
                                        = this->m_pData[i][j];
                            }
                        }
                    }
                    double det = 0;
                    for (int k = 0; k < DIM; k++)
                    {
                        if ((k % 2) == 0)
                            det = det + (this->m_pData[0][k]
                                    * temp[k]->Determinant());
                        else
                            det = det - (this->m_pData[0][k]
                                    * temp[k]->Determinant());
                    }
                    for (int i = 0; i < DIM; i++)
                        delete temp[i];
                    delete[] temp;
                    return det;
                }
                    break;
            }
        }
        CMatrix& operator =(const CMatrix &other)
        {
            if (this->m_rows != other.m_rows || this->m_cols != other.m_cols)
            {
                std::cout
                        << "WARNING: Assignment is taking place with by changing the number of rows and columns of the matrix";
            }
            for (int i = 0; i < m_rows; i++)
                delete[] m_pData[i];
            delete[] m_pData;
            m_rows = m_cols = 0;
            strcpy(m_name, other.m_name);
            m_rows = other.m_rows;
            m_cols = other.m_cols;
            m_pData = new double*[m_rows];
            for (int i = 0; i < m_rows; i++)
                m_pData[i] = new double[m_cols];
            for (int i = 0; i < m_rows; i++)
            {
                for (int j = 0; j < m_cols; j++)
                {
                    m_pData[i][j] = other.m_pData[i][j];
                }
            }
            return *this;
        }
        CMatrix CoFactor()
        {
            CMatrix cofactor("COF", m_rows, m_cols);
            if (m_rows != m_cols)
                return cofactor;
            if (m_rows < 2)
                return cofactor;
            else if (m_rows == 2)
            {
                cofactor.m_pData[0][0] = m_pData[1][1];
                cofactor.m_pData[0][1] = -m_pData[1][0];
                cofactor.m_pData[1][0] = -m_pData[0][1];
                cofactor.m_pData[1][1] = m_pData[0][0];
                return cofactor;
            }
            else if (m_rows >= 3)
            {
                int DIM = m_rows;
                CMatrix ***temp = new CMatrix**[DIM];
                for (int i = 0; i < DIM; i++)
                    temp[i] = new CMatrix*[DIM];
                for (int i = 0; i < DIM; i++)
                    for (int j = 0; j < DIM; j++)
                        temp[i][j] = new CMatrix("", DIM - 1, DIM - 1);
                for (int k1 = 0; k1 < DIM; k1++)
                {
                    for (int k2 = 0; k2 < DIM; k2++)
                    {
                        int i1 = 0;
                        for (int i = 0; i < DIM; i++)
                        {
                            int j1 = 0;
                            for (int j = 0; j < DIM; j++)
                            {
                                if (k1 == i || k2 == j)
                                    continue;
                                temp[k1][k2]->m_pData[i1][j1++]
                                        = this->m_pData[i][j];
                            }
                            if (k1 != i)
                                i1++;
                        }
                    }
                }
                bool flagPositive = true;
                for (int k1 = 0; k1 < DIM; k1++)
                {
                    flagPositive = ((k1 % 2) == 0);
                    for (int k2 = 0; k2 < DIM; k2++)
                    {
                        if (flagPositive == true)
                        {
                            cofactor.m_pData[k1][k2]
                                    = temp[k1][k2]->Determinant();
                            flagPositive = false;
                        }
                        else
                        {
                            cofactor.m_pData[k1][k2]
                                    = -temp[k1][k2]->Determinant();
                            flagPositive = true;
                        }
                    }
                }
                for (int i = 0; i < DIM; i++)
                    for (int j = 0; j < DIM; j++)
                        delete temp[i][j];
                for (int i = 0; i < DIM; i++)
                    delete[] temp[i];
                delete[] temp;
            }
            return cofactor;
        }
        CMatrix Adjoint()
        {
            CMatrix cofactor("COF", m_rows, m_cols);
            CMatrix adj("ADJ", m_rows, m_cols);
            if (m_rows != m_cols)
                return adj;
            cofactor = this->CoFactor();
            // adjoint is transpose of a cofactor of a matrix
            for (int i = 0; i < m_rows; i++)
            {
                for (int j = 0; j < m_cols; j++)
                {
                    adj.m_pData[j][i] = cofactor.m_pData[i][j];
                }
            }
            return adj;
        }
        CMatrix Transpose()
        {
            CMatrix trans("TR", m_cols, m_rows);
            for (int i = 0; i < m_rows; i++)
            {
                for (int j = 0; j < m_cols; j++)
                {
                    trans.m_pData[j][i] = m_pData[i][j];
                }
            }
            return trans;
        }
        CMatrix Inverse()
        {
            CMatrix cofactor("COF", m_rows, m_cols);
            CMatrix inv("INV", m_rows, m_cols);
            if (m_rows != m_cols)
                return inv;
            // to find out Determinant
            double det = Determinant();
            cofactor = this->CoFactor();
            // inv = transpose of cofactor / Determinant
            for (int i = 0; i < m_rows; i++)
            {
                for (int j = 0; j < m_cols; j++)
                {
                    inv.m_pData[j][i] = cofactor.m_pData[i][j] / det;
                }
            }
            return inv;
        }
        CMatrix operator +(const CMatrix &other)
        {
            if (this->m_rows != other.m_rows || this->m_cols != other.m_cols)
            {
                std::cout
                        << "Addition could not take place because number of rows and columns are different between the two matrices";
                return *this;
            }
            CMatrix result("", m_rows, m_cols);
            for (int i = 0; i < m_rows; i++)
            {
                for (int j = 0; j < m_cols; j++)
                {
                    result.m_pData[i][j] = this->m_pData[i][j]
                            + other.m_pData[i][j];
                }
            }
            return result;
        }
        CMatrix operator -(const CMatrix &other)
        {
            if (this->m_rows != other.m_rows || this->m_cols != other.m_cols)
            {
                std::cout
                        << "Subtraction could not take place because number of rows and columns are different between the two matrices";
                return *this;
            }
            CMatrix result("", m_rows, m_cols);
            for (int i = 0; i < m_rows; i++)
            {
                for (int j = 0; j < m_cols; j++)
                {
                    result.m_pData[i][j] = this->m_pData[i][j]
                            - other.m_pData[i][j];
                }
            }
            return result;
        }
        CMatrix operator *(const CMatrix &other)
        {
            if (this->m_cols != other.m_rows)
            {
                std::cout
                        << "Multiplication could not take place because number of columns of 1st Matrix and number of rows in 2nd Matrix are different";
                return *this;
            }
            CMatrix result("", this->m_rows, other.m_cols);
            for (int i = 0; i < this->m_rows; i++)
            {
                for (int j = 0; j < other.m_cols; j++)
                {
                    for (int k = 0; k < this->m_cols; k++)
                    {
                        result.m_pData[i][j] += this->m_pData[i][k]
                                * other.m_pData[k][j];
                    }
                }
            }
            return result;
        }
        bool operator ==(const CMatrix &other)
        {
            if (this->m_rows != other.m_rows || this->m_cols != other.m_cols)
            {
                std::cout
                        << "Comparision could not take place because number of rows and columns are different between the two matrices";
                return false;
            }
            CMatrix result("", m_rows, m_cols);
            bool bEqual = true;
            for (int i = 0; i < m_rows; i++)
            {
                for (int j = 0; j < m_cols; j++)
                {
                    if (this->m_pData[i][j] != other.m_pData[i][j])
                        bEqual = false;
                }
            }
            return bEqual;
        }
        friend std::istream& operator >>(std::istream &is, CMatrix &m);
        friend std::ostream& operator <<(std::ostream &os, const CMatrix &m);
};
std::istream& operator >>(std::istream &is, CMatrix &m)
{
    std::cout << "\n\nEnter Input For Matrix : " << m.m_name << " Rows: "
            << m.m_rows << " Cols: " << m.m_cols << "\n";
    for (int i = 0; i < m.m_rows; i++)
    {
        for (int j = 0; j < m.m_cols; j++)
        {
            std::cout << "Input For Row: " << i + 1 << " Col: " << j + 1
                    << " = ";
            is >> m.m_pData[i][j];
        }
        std::cout << "\n";
    }
    std::cout << "\n";
    return is;
}
std::ostream& operator <<(std::ostream &os, const CMatrix &m)
{
    os << "\n\nMatrix : " << m.m_name << " Rows: " << m.m_rows << " Cols: "
            << m.m_cols << "\n\n";
    for (int i = 0; i < m.m_rows; i++)
    {
        os << " | ";
        for (int j = 0; j < m.m_cols; j++)
        {
            char buf[32];
            double data = m.m_pData[i][j];
            if (m.m_pData[i][j] > -0.00001 && m.m_pData[i][j] < 0.00001)
                data = 0;
            sprintf(buf, "%10.2lf ", data);
            os << buf;
        }
        os << "|\n";
    }
    os << "\n\n";
    return os;
}
#endif
int main()
{
    CMatrix a("A", 6, 6);
    CMatrix b("B", 6, 1);
    a.FillSimulatedInput();
    b.FillSimulatedInput();
    std::cout << a << "\n Determinant : ";
    std::cout << a.Determinant() << "\n";
    std::cout << b << "\n Determinant : ";
    std::cout << b.Determinant() << "\n";
    CMatrix ainv = a.Inverse();
    CMatrix q = a * ainv;
    q.SetName("A * A'");
    std::cout << q << "\n";
    CMatrix x = ainv * b;
    x.SetName("X");
    std::cout << x << "\n";
    CMatrix y = a * x; // we will get B
    y.SetName("Y");
    std::cout << y << "\n";
    return 0;
}
